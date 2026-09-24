#include "ratehub/fleet.hpp"

#include <cstdint>
#include <cstdio>

namespace {

int g_failed = 0;

void expect(bool cond, const char* name) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s\n", name);
        ++g_failed;
    }
}

}  // namespace

int test_fleet() {
    ratehub::FleetSoA fleet;
    ratehub::Record sample;
    sample.source_id = 1;
    sample.sequence = 1;
    sample.pos_x = 1000;
    sample.pos_y = 0;
    sample.vel_x = 2000;
    sample.vel_y = 0;

    expect(ratehub::integrate(fleet, sample, ratehub::kFastPeriodNs) == ratehub::IntegrateStatus::Ok, "first step");
    // 2000 mm/s * 5 ms = 10 mm, added to the sample position.
    expect(fleet.pos_x[1] == 1010, "5 ms step is 10 mm");
    expect(fleet.valid[1], "row marked valid");

    const std::int32_t kept = fleet.pos_x[1];
    sample.pos_x = INT32_MAX;
    sample.vel_x = 1000;
    fleet.valid[1] = false;
    expect(ratehub::integrate(fleet, sample, 1000000000LL) == ratehub::IntegrateStatus::OutOfRange, "range");
    expect(fleet.pos_x[1] == kept && !fleet.valid[1], "rejected step does not store");

    sample.source_id = 64;
    expect(ratehub::integrate(fleet, sample, 0) == ratehub::IntegrateStatus::BadSource, "bad source");
    expect(ratehub::integrate(fleet, sample, -1) == ratehub::IntegrateStatus::BadSource, "source checked first");
    sample.source_id = 1;
    expect(ratehub::integrate(fleet, sample, -1) == ratehub::IntegrateStatus::BadDt, "negative dt");
    return g_failed;
}
