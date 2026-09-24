#pragma once

// Shared bounds for every translation unit. A change here is a schema change:
// kSchemaVersion must move with it, and a process that still has the old
// value must refuse to map the region.

#include <cstddef>
#include <cstdint>

namespace ratehub {

// 64 slots keeps the hot arrays near 1 KB (4 * 64 * 4 bytes) so the 5 ms
// loop can stay in L1. The id on the wire is the index. There is no hash
// table on the hot path.
constexpr std::uint32_t kMaxSources = 64;

// Bumped when a field is added, reordered, or resized. Not bumped for a
// comment. decode_frame rejects any other value.
// 2: per-rate heartbeats and overrun counters in the shared control block.
// The 38-byte frame layout did not change.
constexpr std::uint16_t kSchemaVersion = 2;

// Chosen so a hex dump is obvious and does not collide with a zeroed buffer.
constexpr std::uint16_t kFrameMagic = 0xA15C;

// Wire size of one record. See the offset table in frame.hpp. The native
// Record below is not this size and is not what crosses a process boundary.
constexpr std::size_t kFrameBytes = 38;

// Soft periods. A miss is counted. The process is not killed for one late
// cycle. These are not a claim of hard real-time on Linux.
constexpr std::int64_t kFastPeriodNs = 5'000'000;
constexpr std::int64_t kWindowPeriodNs = 25'000'000;
constexpr std::int64_t kSnapshotPeriodNs = 100'000'000;

// Native record after a successful decode.
// pos_* are millimeters. vel_* are millimeters per second.
// time_ns is the source clock, not the local steady clock.
// This struct is trivially copyable so it can sit in a ring slot.
struct Record {
    std::uint32_t source_id = 0;
    std::uint32_t sequence = 0;
    std::int64_t time_ns = 0;
    std::int32_t pos_x = 0;
    std::int32_t pos_y = 0;
    std::int32_t vel_x = 0;
    std::int32_t vel_y = 0;
};

// decode_frame's result. On every value except Ok the caller's Record is
// left as it was. Callers count Bad* and keep the stream.
enum class ParseStatus {
    Ok,
    Truncated,
    BadMagic,
    BadVersion,
    BadSource,
    BadChecksum,
};

// integrate's result. OutOfRange, BadDt, and BadSource leave the fleet row
// unchanged. The caller does not publish that row.
enum class IntegrateStatus {
    Ok,
    BadSource,
    BadDt,
    OutOfRange,
};

}  // namespace ratehub
