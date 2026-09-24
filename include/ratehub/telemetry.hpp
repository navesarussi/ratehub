#pragma once

// Observer-only snapshot of Layout. collect_telemetry acquire-loads atomics
// and ring indexes. It never stores to Layout. format_telemetry_json writes
// into a caller buffer with no heap. 0 means the buffer was too small.

#include "ratehub/layout.hpp"

#include <cstddef>
#include <cstdint>

namespace ratehub {

struct TelemetrySnapshot {
    std::uint64_t ts_ns = 0;
    std::uint32_t generation = 0;
    std::uint32_t shutdown = 0;
    std::uint32_t fault = 0;
    std::uint32_t idle = 0;
    std::uint32_t ring_occupied = 0;
    std::uint32_t ring_capacity = 0;
    std::uint32_t overrun_fast = 0;
    std::uint32_t overrun_window = 0;
    std::uint32_t overrun_snapshot = 0;
    std::uint32_t ingest_done = 0;
    std::uint32_t fast_done = 0;
    std::uint32_t window_done = 0;
    std::uint32_t snapshot_done = 0;
    std::uint32_t compute_done = 0;
    std::uint64_t hb_ingest = 0;
    std::uint64_t hb_compute = 0;
    std::uint64_t hb_publish = 0;
    std::uint64_t hb_fast = 0;
    std::uint64_t hb_window = 0;
    std::uint64_t hb_snapshot = 0;
    std::uint32_t ingest_pid = 0;
    std::uint32_t compute_pid = 0;
    std::uint32_t publish_pid = 0;
    std::uint32_t observe_pid = 0;
    std::uint32_t ingest_exit = 0;
    std::uint32_t compute_exit = 0;
    std::uint32_t publish_exit = 0;
    struct SourcePoint {
        std::uint32_t id = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint32_t publishes = 0;
        std::uint32_t sequence = 0;
    };
    static constexpr std::size_t kMaxPoints = 16;
    SourcePoint sources[kMaxPoints]{};
    std::size_t source_count = 0;
};

TelemetrySnapshot collect_telemetry(const Layout& layout, std::uint64_t now_ns) noexcept;
std::size_t format_telemetry_json(const TelemetrySnapshot& snap, char* out, std::size_t cap) noexcept;

}  // namespace ratehub
