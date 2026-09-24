#include "ratehub/roles.hpp"

#include "ratehub/pace.hpp"
#include "ratehub/shm_region.hpp"

#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

extern char** environ;

namespace ratehub {
namespace {

constexpr int kMaxComputeAttempts = 3;

volatile std::sig_atomic_t g_hold_stop = 0;

void hold_signal(int) {
    g_hold_stop = 1;
}

pid_t spawn_child(const char* exe, const char* role, const char* shm, const char* extra) {
    char* argv[5];
    argv[0] = const_cast<char*>(exe);
    argv[1] = const_cast<char*>(role);
    argv[2] = const_cast<char*>(shm);
    int argc = 3;
    if (extra != nullptr) {
        argv[argc++] = const_cast<char*>(extra);
    }
    argv[argc] = nullptr;
    pid_t pid = 0;
    if (posix_spawn(&pid, exe, nullptr, nullptr, argv, environ) != 0) {
        return -1;
    }
    return pid;
}

int wait_child(pid_t pid) {
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0) {
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
}

void store_pid(std::atomic<std::uint32_t>& slot, pid_t pid) {
    if (pid > 0) {
        slot.store(static_cast<std::uint32_t>(pid), std::memory_order_release);
    }
}

void store_exit(std::atomic<std::uint32_t>& slot, int code) {
    slot.store(static_cast<std::uint32_t>(code), std::memory_order_release);
}

void reset_control(Layout& layout) {
    ShmControl& control = layout.control;
    control.shutdown.store(0, std::memory_order_release);
    control.ingest_done.store(0, std::memory_order_release);
    control.fast_done.store(0, std::memory_order_release);
    control.window_done.store(0, std::memory_order_release);
    control.snapshot_done.store(0, std::memory_order_release);
    control.compute_done.store(0, std::memory_order_release);
    control.fault.store(0, std::memory_order_release);
    control.overrun_fast.store(0, std::memory_order_relaxed);
    control.overrun_window.store(0, std::memory_order_relaxed);
    control.overrun_snapshot.store(0, std::memory_order_relaxed);
    control.heartbeat_ingest.store(0, std::memory_order_release);
    control.heartbeat_compute.store(0, std::memory_order_release);
    control.heartbeat_publish.store(0, std::memory_order_release);
    control.heartbeat_fast.store(0, std::memory_order_release);
    control.heartbeat_window.store(0, std::memory_order_release);
    control.heartbeat_snapshot.store(0, std::memory_order_release);
    SupervisorState& sup = layout.supervisor;
    sup.ingest_pid.store(0, std::memory_order_release);
    sup.compute_pid.store(0, std::memory_order_release);
    sup.publish_pid.store(0, std::memory_order_release);
    sup.ingest_exit.store(0, std::memory_order_release);
    sup.compute_exit.store(0, std::memory_order_release);
    sup.publish_exit.store(0, std::memory_order_release);
    control.generation.fetch_add(1, std::memory_order_release);
}

int run_once_pipeline(Layout& layout, const char* exe, const char* shm, const char* replay_path, const char* out_path) {
    const pid_t ingest = spawn_child(exe, "ingest", shm, replay_path);
    store_pid(layout.supervisor.ingest_pid, ingest);
    const pid_t publish = spawn_child(exe, "publish", shm, out_path);
    store_pid(layout.supervisor.publish_pid, publish);
    bool compute_ok = false;
    int compute_status = 1;
    for (int attempt = 1; attempt <= kMaxComputeAttempts; ++attempt) {
        const pid_t compute = spawn_child(exe, "compute", shm, nullptr);
        store_pid(layout.supervisor.compute_pid, compute);
        compute_status = wait_child(compute);
        store_exit(layout.supervisor.compute_exit, compute_status);
        if (compute_status == 0) {
            compute_ok = true;
            break;
        }
        if (attempt == kMaxComputeAttempts) {
            break;
        }
        layout.control.generation.fetch_add(1, std::memory_order_release);
    }
    layout.control.shutdown.store(1, std::memory_order_release);
    const int ingest_status = wait_child(ingest);
    store_exit(layout.supervisor.ingest_exit, ingest_status);
    const int publish_status = wait_child(publish);
    store_exit(layout.supervisor.publish_exit, publish_status);
    if (!compute_ok || ingest_status != 0 || publish_status != 0) {
        return 1;
    }
    return 0;
}

}  // namespace

int run_supervisor(const char* exe, const char* replay_path, const char* out_path, std::uint16_t observe_port) {
    const std::string name = "/rh-" + std::to_string(static_cast<long long>(getpid()));
    ShmMapping mapping;
    if (shm_create(name.c_str(), mapping) != ShmStatus::Ok) {
        return 1;
    }
    pid_t observe = -1;
    char port_buf[16];
    if (observe_port != 0) {
        std::snprintf(port_buf, sizeof(port_buf), "%u", static_cast<unsigned>(observe_port));
        observe = spawn_child(exe, "observe", name.c_str(), port_buf);
        store_pid(mapping.layout->supervisor.observe_pid, observe);
        if (pacing_enabled()) {
            std::fprintf(stderr, "ratehub dashboard http://127.0.0.1:%u/  (Run again in the page, Ctrl+C to stop)\n",
                         static_cast<unsigned>(observe_port));
        }
    }
    bool first = true;
    int last_status = 1;
    for (;;) {
        if (!first) {
            reset_control(*mapping.layout);
        }
        first = false;
        last_status = run_once_pipeline(*mapping.layout, exe, name.c_str(), replay_path, out_path);
        if (observe < 0 || !pacing_enabled()) {
            break;
        }
        g_hold_stop = 0;
        std::signal(SIGINT, hold_signal);
        std::signal(SIGTERM, hold_signal);
        bool rerun = false;
        while (g_hold_stop == 0) {
            if (take_rerun(mapping.layout->supervisor)) {
                rerun = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!rerun) {
            break;
        }
    }
    int observe_status = 0;
    if (observe >= 0) {
        mapping.layout->supervisor.observe_release.store(1, std::memory_order_release);
        observe_status = wait_child(observe);
    }
    shm_close(mapping);
    if (last_status != 0 || observe_status != 0) {
        return 1;
    }
    return 0;
}

}  // namespace ratehub
