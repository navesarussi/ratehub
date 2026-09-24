#pragma once

// FleetSoA — private working set of the 5 ms thread.
//
// One writer. No other thread reads these arrays. The 25 ms thread reads a
// seqlock the 5 ms thread publishes after integrate() returns. Reading this
// object from another thread is a data race, not a stale sample.
//
// Layout
//   Each component is its own array of kMaxSources int32 values.
//   64 * 4 = 256 bytes, four cache lines per component, about 1 KB for the
//   four position and velocity arrays. A structure-of-arrays walk touches
//   one component for every source and then the next component. An array of
//   structs would load position and velocity together and throw half of
//   each line away when the loop only needs one.
//
// Scale
//   Positions are millimeters. Velocities are millimeters per second.
//   dt_ns is nanoseconds. The step is
//     next = pos + vel * dt_ns / 1_000_000_000
//   computed in int64. There is no floating point, so two runs with the same
//   inputs produce the same row.
//
// Store rule
//   Both axes are computed before any store. If either product would overflow
//   int64, or either next value does not fit in int32, the function returns
//   OutOfRange and the row is unchanged. Signed overflow is undefined. The
//   code does not wrap.
//
// First sample
//   A row with valid == false integrates from the sample's own position, not
//   from zero. The first publish is "where the source said it was, plus one
//   step", not a jump from the origin.

#include "ratehub/types.hpp"

#include <cstdint>

namespace ratehub {

struct FleetSoA {
    std::int32_t pos_x[kMaxSources]{};
    std::int32_t pos_y[kMaxSources]{};
    std::int32_t vel_x[kMaxSources]{};
    std::int32_t vel_y[kMaxSources]{};
    std::uint32_t sequence[kMaxSources]{};
    std::int64_t time_ns[kMaxSources]{};
    bool valid[kMaxSources]{};
};

// On Ok, source_id's row matches the sample's velocity, sequence, and time,
// and the position has moved by one step. On any other status the row is
// unchanged, including `valid`.
IntegrateStatus integrate(FleetSoA& fleet, const Record& sample, std::int64_t dt_ns) noexcept;

}  // namespace ratehub
