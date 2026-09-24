#include "ratehub/fleet.hpp"

#include <cstdint>

// step() rejects a multiply that would overflow int64 before it happens.
// The divide by 1e9 is toward zero, which is the C++ rule for integers.
// A 5 ms step of 1 mm/s therefore contributes 0 mm. That truncation is
// intentional. Sub-millimeter residue is not kept.

namespace ratehub {
namespace {

bool step(std::int32_t pos, std::int32_t vel, std::int64_t dt_ns, std::int64_t& next) noexcept {
    if (vel != 0) {
        const std::int64_t mag = vel > 0 ? vel : -static_cast<std::int64_t>(vel);
        if (dt_ns > INT64_MAX / mag) {
            return false;
        }
    }
    next = static_cast<std::int64_t>(pos) + static_cast<std::int64_t>(vel) * dt_ns / 1000000000LL;
    return next <= INT32_MAX && next >= INT32_MIN;
}

}  // namespace

IntegrateStatus integrate(FleetSoA& fleet, const Record& sample, std::int64_t dt_ns) noexcept {
    if (sample.source_id >= kMaxSources) {
        return IntegrateStatus::BadSource;
    }
    if (dt_ns < 0) {
        return IntegrateStatus::BadDt;
    }
    const std::uint32_t id = sample.source_id;
    const std::int32_t base_x = fleet.valid[id] ? fleet.pos_x[id] : sample.pos_x;
    const std::int32_t base_y = fleet.valid[id] ? fleet.pos_y[id] : sample.pos_y;
    std::int64_t next_x = 0;
    std::int64_t next_y = 0;
    if (!step(base_x, sample.vel_x, dt_ns, next_x) || !step(base_y, sample.vel_y, dt_ns, next_y)) {
        return IntegrateStatus::OutOfRange;
    }
    fleet.pos_x[id] = static_cast<std::int32_t>(next_x);
    fleet.pos_y[id] = static_cast<std::int32_t>(next_y);
    fleet.vel_x[id] = sample.vel_x;
    fleet.vel_y[id] = sample.vel_y;
    fleet.sequence[id] = sample.sequence;
    fleet.time_ns[id] = sample.time_ns;
    fleet.valid[id] = true;
    return IntegrateStatus::Ok;
}

}  // namespace ratehub
