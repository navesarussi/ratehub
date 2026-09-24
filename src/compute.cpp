#include "ratehub/roles.hpp"

#include "ratehub/fleet.hpp"
#include "ratehub/pace.hpp"
#include "ratehub/shm_region.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

namespace ratehub {
namespace {

bool stopped(const ShmControl& control) {
    return control.shutdown.load(std::memory_order_acquire) != 0;
}

bool crash_requested(const char* shm_name) {
    const char* crash = std::getenv("RATEHUB_CRASH");
    if (crash == nullptr) {
        return false;
    }
    if (std::strcmp(crash, "always") == 0) {
        return true;
    }
    if (std::strcmp(crash, "once") != 0) {
        return false;
    }
    std::string marker = "/tmp/ratehub-once";
    for (const char* p = shm_name; *p != '\0'; ++p) {
        marker.push_back(*p == '/' ? '_' : *p);
    }
    std::ifstream existing(marker);
    if (existing.good()) {
        return false;
    }
    std::ofstream created(marker);
    return created.good();
}

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count());
}

void beat(std::atomic<std::uint64_t>& slot) {
    slot.store(now_ns(), std::memory_order_release);
}

void finish_cycle(std::atomic<std::uint32_t>& overruns, std::int64_t work_ns, std::int64_t period_ns) {
    const Budget spent = budget(work_ns, period_ns);
    if (spent.overrun) {
        overruns.fetch_add(1, std::memory_order_relaxed);
    }
    if (pacing_enabled() && spent.sleep_ns > 0) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(spent.sleep_ns));
        return;
    }
    std::this_thread::yield();
}

void publish_windows(Layout& layout) {
    for (std::uint32_t id = 0; id < kMaxSources; ++id) {
        SourceState source;
        if (!layout.latest[id].load(source) || source.valid == 0) {
            continue;
        }
        WindowState window;
        window.pos_x = source.pos_x;
        window.pos_y = source.pos_y;
        window.sequence = source.sequence;
        window.publishes = source.publishes;
        layout.window[id].publish(window);
    }
}

}  // namespace

int run_compute(const char* shm_name) {
    if (crash_requested(shm_name)) {
        return 2;
    }
    ShmMapping mapping;
    if (shm_open_existing(shm_name, mapping) != ShmStatus::Ok) {
        return 1;
    }
    Layout& layout = *mapping.layout;
    FleetSoA fleet;
    std::uint32_t publishes[kMaxSources]{};

    std::thread fast([&] {
        while (!stopped(layout.control)) {
            beat(layout.control.heartbeat_fast);
            const std::uint64_t started = now_ns();
            bool drained = false;
            Record record;
            while (layout.ring.try_pop(record)) {
                drained = true;
                if (integrate(fleet, record, kFastPeriodNs) != IntegrateStatus::Ok) {
                    continue;
                }
                const std::uint32_t id = record.source_id;
                SourceState state;
                state.pos_x = fleet.pos_x[id];
                state.pos_y = fleet.pos_y[id];
                state.vel_x = fleet.vel_x[id];
                state.vel_y = fleet.vel_y[id];
                state.sequence = fleet.sequence[id];
                state.time_ns = fleet.time_ns[id];
                state.publishes = ++publishes[id];
                state.valid = 1;
                layout.latest[id].publish(state);
                layout.control.heartbeat_compute.store(state.publishes, std::memory_order_release);
            }
            const std::int64_t work = static_cast<std::int64_t>(now_ns() - started);
            if (!drained && layout.control.ingest_done.load(std::memory_order_acquire) != 0) {
                break;
            }
            finish_cycle(layout.control.overrun_fast, work, kFastPeriodNs);
        }
        layout.control.fast_done.store(1, std::memory_order_release);
    });

    std::thread window([&] {
        while (layout.control.fast_done.load(std::memory_order_acquire) == 0 && !stopped(layout.control)) {
            beat(layout.control.heartbeat_window);
            const std::uint64_t started = now_ns();
            publish_windows(layout);
            finish_cycle(layout.control.overrun_window, static_cast<std::int64_t>(now_ns() - started), kWindowPeriodNs);
        }
        publish_windows(layout);
        beat(layout.control.heartbeat_window);
        layout.control.window_done.store(1, std::memory_order_release);
    });

    std::thread snapshot([&] {
        while (layout.control.window_done.load(std::memory_order_acquire) == 0 && !stopped(layout.control)) {
            beat(layout.control.heartbeat_snapshot);
            const std::uint64_t started = now_ns();
            finish_cycle(layout.control.overrun_snapshot, static_cast<std::int64_t>(now_ns() - started), kSnapshotPeriodNs);
        }
        Snapshot snap;
        snap.generation = layout.control.generation.load(std::memory_order_acquire);
        for (std::uint32_t id = 0; id < kMaxSources; ++id) {
            WindowState row;
            if (layout.window[id].load(row)) {
                snap.rows[id] = row;
            }
        }
        layout.snapshots.write_slot() = snap;
        layout.snapshots.publish();
        beat(layout.control.heartbeat_snapshot);
        layout.control.snapshot_done.store(1, std::memory_order_release);
    });

    std::thread watchdog([&] {
        while (layout.control.snapshot_done.load(std::memory_order_acquire) == 0 && !stopped(layout.control)) {
            const std::uint64_t now = now_ns();
            const bool fast_done = layout.control.fast_done.load(std::memory_order_acquire) != 0;
            const bool window_done = layout.control.window_done.load(std::memory_order_acquire) != 0;
            const bool late_fast = heartbeat_stalled(now, layout.control.heartbeat_fast.load(std::memory_order_acquire),
                                                      static_cast<std::uint64_t>(4 * kFastPeriodNs), fast_done);
            const bool late_window = heartbeat_stalled(now, layout.control.heartbeat_window.load(std::memory_order_acquire),
                                                        static_cast<std::uint64_t>(4 * kWindowPeriodNs), window_done);
            const bool late_snapshot = heartbeat_stalled(now, layout.control.heartbeat_snapshot.load(std::memory_order_acquire),
                                                          static_cast<std::uint64_t>(4 * kSnapshotPeriodNs), false);
            if (late_fast || late_window || late_snapshot) {
                layout.control.fault.store(1, std::memory_order_release);
            }
            if (pacing_enabled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                std::this_thread::yield();
            }
        }
    });

    fast.join();
    window.join();
    snapshot.join();
    watchdog.join();
    layout.control.compute_done.store(1, std::memory_order_release);
    const bool fault = layout.control.fault.load(std::memory_order_acquire) != 0;
    shm_close(mapping);
    return fault ? 3 : 0;
}

}  // namespace ratehub
