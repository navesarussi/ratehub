#pragma once

// One shared mapping. The supervisor constructs it. Children attach and use
// the same addresses only after the constructor has finished. Do not memset
// this object: the atomics and the triple buffer have constructors.
//
// latest[i] is the 5 ms publication for source i.
// window[i] is the 25 ms sample of that publication. It carries the latest
// position and the total publish count. It does not reconstruct positions
// the 25 ms thread never observed. The seqlock keeps only the newest value.
//
// The ring holds 1023 records. One slot stays empty so full and empty differ.

#include "ratehub/seqlock.hpp"
#include "ratehub/shm_control.hpp"
#include "ratehub/spsc_ring.hpp"
#include "ratehub/triple_buffer.hpp"
#include "ratehub/types.hpp"

#include <cstdint>

namespace ratehub {

struct SourceState {
    std::int32_t pos_x = 0;
    std::int32_t pos_y = 0;
    std::int32_t vel_x = 0;
    std::int32_t vel_y = 0;
    std::uint32_t sequence = 0;
    std::uint32_t publishes = 0;
    std::int64_t time_ns = 0;
    std::uint32_t valid = 0;
};

struct WindowState {
    std::int32_t pos_x = 0;
    std::int32_t pos_y = 0;
    std::uint32_t sequence = 0;
    std::uint32_t publishes = 0;
};

struct Snapshot {
    std::uint32_t generation = 0;
    WindowState rows[kMaxSources]{};
};

inline constexpr std::uint32_t kRingCapacity = 1024;

struct alignas(64) Layout {
    ShmControl control;
    alignas(64) SpscRing<Record, kRingCapacity> ring;
    alignas(64) Seqlock<SourceState> latest[kMaxSources];
    alignas(64) Seqlock<WindowState> window[kMaxSources];
    alignas(64) TripleBuffer<Snapshot> snapshots;
};

}  // namespace ratehub
