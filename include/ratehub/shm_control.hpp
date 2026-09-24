#pragma once

// Control block for the shared mapping. The supervisor placement-news the
// surrounding Layout, then spawns children. A mismatch on magic or schema
// refuses to attach. See docs/DESIGN.md, "Shared mapping".
//
// The block is standard-layout and contains only lock-free atomics, so it
// can live in a POSIX shared mapping. A mutex in this struct would require
// PTHREAD_PROCESS_SHARED and would be the wrong tool for a shutdown flag.
//
// Heartbeats are separate cache lines. The supervisor reads all three. If
// they shared a line, each child's store would invalidate the others. That
// traffic is false sharing on a path that is already polled.
//
// shutdown is 0 or 1. The writer is the supervisor. Children acquire-load it
// once per cycle. release on the store is enough. seq_cst would order this
// flag with unrelated atomics and is not required.
//
// generation starts at 1. The supervisor increments it with release when it
// restarts compute. Publish acquire-loads it. A change means the snapshot
// is not a continuation of the previous stream.

#include "ratehub/types.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ratehub {

inline constexpr std::uint32_t kShmMagic = 0x52484C59u;  // 'RHLY'

struct alignas(64) ShmControl {
    std::uint32_t magic = kShmMagic;
    std::uint16_t schema = kSchemaVersion;
    std::uint16_t reserved = 0;
    std::atomic<std::uint32_t> shutdown{0};
    std::atomic<std::uint32_t> generation{1};
    // Written once, by the process that owns that stage. 0 or 1.
    // Publish waits on compute_done. Compute's window waits on fast_done.
    std::atomic<std::uint32_t> ingest_done{0};
    std::atomic<std::uint32_t> fast_done{0};
    std::atomic<std::uint32_t> window_done{0};
    std::atomic<std::uint32_t> compute_done{0};
    std::atomic<std::uint32_t> fault{0};
    std::atomic<std::uint32_t> snapshot_done{0};
    // fetch_add once per cycle whose work exceeded its period.
    std::atomic<std::uint32_t> overrun_fast{0};
    std::atomic<std::uint32_t> overrun_window{0};
    std::atomic<std::uint32_t> overrun_snapshot{0};
    alignas(64) std::atomic<std::uint64_t> heartbeat_ingest{0};
    alignas(64) std::atomic<std::uint64_t> heartbeat_compute{0};
    alignas(64) std::atomic<std::uint64_t> heartbeat_publish{0};
    // Steady-clock nanoseconds, one writer each. The compute watchdog reads
    // them. Separate lines so a beat does not invalidate the other two.
    alignas(64) std::atomic<std::uint64_t> heartbeat_fast{0};
    alignas(64) std::atomic<std::uint64_t> heartbeat_window{0};
    alignas(64) std::atomic<std::uint64_t> heartbeat_snapshot{0};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "shutdown must be lock-free in shared memory");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "heartbeat must be lock-free in shared memory");
static_assert(offsetof(ShmControl, heartbeat_ingest) % 64 == 0, "ingest heartbeat starts a cache line");
static_assert(offsetof(ShmControl, heartbeat_compute) % 64 == 0, "compute heartbeat starts a cache line");
static_assert(offsetof(ShmControl, heartbeat_publish) % 64 == 0, "publish heartbeat starts a cache line");
static_assert(offsetof(ShmControl, heartbeat_fast) % 64 == 0, "fast heartbeat starts a cache line");
static_assert(offsetof(ShmControl, heartbeat_window) % 64 == 0, "window heartbeat starts a cache line");
static_assert(offsetof(ShmControl, heartbeat_snapshot) % 64 == 0, "snapshot heartbeat starts a cache line");

}  // namespace ratehub
