#include "ratehub/telemetry.hpp"

#include <cstdarg>
#include <cstdio>

namespace ratehub {
namespace {

bool put(char*& p, const char* end, const char* fmt, ...) noexcept {
    if (p == nullptr || end == nullptr || p >= end) {
        return false;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(p, static_cast<std::size_t>(end - p), fmt, ap);
    va_end(ap);
    if (n < 0 || p + n >= end) {
        return false;
    }
    p += n;
    return true;
}

}  // namespace

TelemetrySnapshot collect_telemetry(const Layout& layout, std::uint64_t now_ns) noexcept {
    TelemetrySnapshot snap;
    snap.ts_ns = now_ns;
    snap.generation = layout.control.generation.load(std::memory_order_acquire);
    snap.shutdown = layout.control.shutdown.load(std::memory_order_acquire);
    snap.fault = layout.control.fault.load(std::memory_order_acquire);
    snap.ring_occupied = layout.ring.occupied();
    snap.ring_capacity = kRingCapacity - 1;
    snap.overrun_fast = layout.control.overrun_fast.load(std::memory_order_acquire);
    snap.overrun_window = layout.control.overrun_window.load(std::memory_order_acquire);
    snap.overrun_snapshot = layout.control.overrun_snapshot.load(std::memory_order_acquire);
    snap.ingest_done = layout.control.ingest_done.load(std::memory_order_acquire);
    snap.fast_done = layout.control.fast_done.load(std::memory_order_acquire);
    snap.window_done = layout.control.window_done.load(std::memory_order_acquire);
    snap.snapshot_done = layout.control.snapshot_done.load(std::memory_order_acquire);
    snap.compute_done = layout.control.compute_done.load(std::memory_order_acquire);
    snap.hb_ingest = layout.control.heartbeat_ingest.load(std::memory_order_acquire);
    snap.hb_compute = layout.control.heartbeat_compute.load(std::memory_order_acquire);
    snap.hb_publish = layout.control.heartbeat_publish.load(std::memory_order_acquire);
    snap.hb_fast = layout.control.heartbeat_fast.load(std::memory_order_acquire);
    snap.hb_window = layout.control.heartbeat_window.load(std::memory_order_acquire);
    snap.hb_snapshot = layout.control.heartbeat_snapshot.load(std::memory_order_acquire);
    snap.ingest_pid = layout.supervisor.ingest_pid.load(std::memory_order_acquire);
    snap.compute_pid = layout.supervisor.compute_pid.load(std::memory_order_acquire);
    snap.publish_pid = layout.supervisor.publish_pid.load(std::memory_order_acquire);
    snap.observe_pid = layout.supervisor.observe_pid.load(std::memory_order_acquire);
    snap.ingest_exit = layout.supervisor.ingest_exit.load(std::memory_order_acquire);
    snap.compute_exit = layout.supervisor.compute_exit.load(std::memory_order_acquire);
    snap.publish_exit = layout.supervisor.publish_exit.load(std::memory_order_acquire);
    snap.idle = (snap.shutdown != 0 && snap.ingest_done != 0 && snap.compute_done != 0) ? 1u : 0u;
    for (std::uint32_t id = 0; id < kMaxSources; ++id) {
        WindowState row;
        if (!layout.window[id].load(row) || row.publishes == 0) {
            continue;
        }
        if (snap.source_count >= TelemetrySnapshot::kMaxPoints) {
            break;
        }
        TelemetrySnapshot::SourcePoint& point = snap.sources[snap.source_count++];
        point.id = id;
        point.x = row.pos_x;
        point.y = row.pos_y;
        point.publishes = row.publishes;
        point.sequence = row.sequence;
    }
    return snap;
}

std::size_t format_telemetry_json(const TelemetrySnapshot& snap, char* out, std::size_t cap) noexcept {
    if (out == nullptr || cap < 2) {
        return 0;
    }
    char* p = out;
    const char* end = out + cap;
    if (!put(p, end,
             "{\"ts_ns\":%llu,\"generation\":%u,\"shutdown\":%u,\"fault\":%u,\"idle\":%u,"
             "\"ring\":{\"occupied\":%u,\"capacity\":%u},"
             "\"overrun\":{\"fast\":%u,\"window\":%u,\"snapshot\":%u},"
             "\"done\":{\"ingest\":%u,\"fast\":%u,\"window\":%u,\"snapshot\":%u,\"compute\":%u},"
             "\"hb\":{\"ingest\":%llu,\"compute\":%llu,\"publish\":%llu,\"fast\":%llu,\"window\":%llu,\"snapshot\":%llu},"
             "\"pids\":{\"ingest\":%u,\"compute\":%u,\"publish\":%u,\"observe\":%u},"
             "\"exits\":{\"ingest\":%u,\"compute\":%u,\"publish\":%u},\"sources\":[",
             static_cast<unsigned long long>(snap.ts_ns), snap.generation, snap.shutdown, snap.fault, snap.idle,
             snap.ring_occupied, snap.ring_capacity, snap.overrun_fast, snap.overrun_window, snap.overrun_snapshot,
             snap.ingest_done, snap.fast_done, snap.window_done, snap.snapshot_done, snap.compute_done,
             static_cast<unsigned long long>(snap.hb_ingest), static_cast<unsigned long long>(snap.hb_compute),
             static_cast<unsigned long long>(snap.hb_publish), static_cast<unsigned long long>(snap.hb_fast),
             static_cast<unsigned long long>(snap.hb_window), static_cast<unsigned long long>(snap.hb_snapshot),
             snap.ingest_pid, snap.compute_pid, snap.publish_pid, snap.observe_pid, snap.ingest_exit, snap.compute_exit,
             snap.publish_exit)) {
        return 0;
    }
    for (std::size_t i = 0; i < snap.source_count; ++i) {
        const TelemetrySnapshot::SourcePoint& src = snap.sources[i];
        if (!put(p, end, "%s{\"id\":%u,\"x\":%d,\"y\":%d,\"p\":%u,\"seq\":%u}", i == 0 ? "" : ",", src.id, src.x, src.y,
                 src.publishes, src.sequence)) {
            return 0;
        }
    }
    if (!put(p, end, "]}")) {
        return 0;
    }
    return static_cast<std::size_t>(p - out);
}

}  // namespace ratehub
