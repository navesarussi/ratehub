#include "ratehub/roles.hpp"

#include "ratehub/http_sse.hpp"
#include "ratehub/pace.hpp"
#include "ratehub/shm_region.hpp"
#include "ratehub/telemetry.hpp"

#include <csignal>
#include <chrono>
#include <cstdio>
#include <thread>
#include <unistd.h>

namespace ratehub {
namespace {

volatile std::sig_atomic_t g_observe_stop = 0;

void observe_signal(int) {
    g_observe_stop = 1;
}

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool released(const Layout& layout) {
    return layout.supervisor.observe_release.load(std::memory_order_acquire) != 0;
}

}  // namespace

int run_observe(const char* shm_name, std::uint16_t port, std::uint32_t interval_ms, const char* web_root) {
    if (interval_ms == 0) {
        interval_ms = 100;
    }
    std::signal(SIGINT, observe_signal);
    std::signal(SIGTERM, observe_signal);
    ShmMapping mapping;
    if (shm_open_existing(shm_name, mapping) != ShmStatus::Ok) {
        return 1;
    }
    HttpSseServer srv;
    if (!http_sse_listen(srv, port)) {
        shm_close(mapping);
        return 1;
    }
    std::fprintf(stderr, "ratehub observe http://127.0.0.1:%u/\n", static_cast<unsigned>(srv.port));
    char json[4096];
    int client_fd = -1;
    const char* root = web_root != nullptr ? web_root : "web";
    while (g_observe_stop == 0 && !released(*mapping.layout)) {
        const TelemetrySnapshot snap = collect_telemetry(*mapping.layout, now_ns());
        const std::size_t n = format_telemetry_json(snap, json, sizeof(json));
        if (n == 0) {
            std::this_thread::yield();
            continue;
        }
        const int timeout = pacing_enabled() ? static_cast<int>(interval_ms) : 0;
        bool reset = false;
        if (!http_sse_pump(srv, client_fd, root, json, n, timeout, &reset)) {
            break;
        }
        if (reset) {
            request_rerun(mapping.layout->supervisor);
        }
        if (pacing_enabled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        } else {
            std::this_thread::yield();
        }
    }
    if (client_fd >= 0) {
        ::close(client_fd);
    }
    http_sse_close(srv);
    shm_close(mapping);
    return 0;
}

}  // namespace ratehub
