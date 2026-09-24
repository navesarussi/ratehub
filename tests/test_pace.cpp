#include "ratehub/pace.hpp"
#include "ratehub/types.hpp"

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

int test_pace() {
    const ratehub::Budget inside = ratehub::budget(1'000'000, ratehub::kFastPeriodNs);
    expect(!inside.overrun && inside.sleep_ns == 4'000'000, "remainder of a 5 ms period");
    const ratehub::Budget late = ratehub::budget(6'000'000, ratehub::kFastPeriodNs);
    expect(late.overrun && late.sleep_ns == 0, "overrun does not sleep");

    expect(!ratehub::heartbeat_stalled(100, 0, 20, false), "unstarted thread is not stalled");
    expect(!ratehub::heartbeat_stalled(100, 90, 20, true), "finished stage is not stalled");
    expect(!ratehub::heartbeat_stalled(100, 90, 20, false), "fresh beat is not stalled");
    expect(ratehub::heartbeat_stalled(100, 70, 20, false), "silence past the limit is a stall");
    return g_failed;
}
