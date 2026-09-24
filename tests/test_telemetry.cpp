#include "ratehub/layout.hpp"
#include "ratehub/telemetry.hpp"

#include <cstdio>
#include <cstring>

int test_telemetry() {
    ratehub::Layout layout;
    layout.control.generation.store(2, std::memory_order_relaxed);
    layout.control.ingest_done.store(1, std::memory_order_relaxed);
    layout.control.compute_done.store(1, std::memory_order_relaxed);
    layout.control.shutdown.store(1, std::memory_order_relaxed);
    layout.ring.try_push(ratehub::Record{1, 1, 0, 100, 0, 10, 0});
    ratehub::WindowState row;
    row.pos_x = 100;
    row.pos_y = 0;
    row.publishes = 1;
    row.sequence = 1;
    layout.window[1].publish(row);
    const auto snap = ratehub::collect_telemetry(layout, 999);
    if (snap.generation != 2 || snap.ring_occupied != 1 || snap.idle != 1) {
        std::fprintf(stderr, "FAIL collect gen=%u occ=%u idle=%u\n", snap.generation, snap.ring_occupied,
                     snap.idle);
        return 1;
    }
    if (snap.source_count != 1 || snap.sources[0].id != 1 || snap.sources[0].x != 100) {
        std::fprintf(stderr, "FAIL sources\n");
        return 1;
    }
    char buf[4096];
    const std::size_t n = ratehub::format_telemetry_json(snap, buf, sizeof(buf));
    if (n == 0 || std::strstr(buf, "\"generation\":2") == nullptr || std::strstr(buf, "\"idle\":1") == nullptr) {
        std::fprintf(stderr, "FAIL json %s\n", buf);
        return 1;
    }
    if (std::strstr(buf, "\"id\":1") == nullptr) {
        std::fprintf(stderr, "FAIL json sources %s\n", buf);
        return 1;
    }
    return 0;
}
