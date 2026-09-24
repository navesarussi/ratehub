#pragma once

// Period budget and silence check. Pure functions so the tests do not sleep.
//
// A cycle that finishes inside its period sleeps the remainder. A cycle that
// runs past the period counts one overrun and does not sleep: sleeping would
// add the lateness to the next deadline. The caller decides whether the
// sleep actually happens. RATEHUB_PACE=0 skips it so a replay test stays
// independent of the wall clock. The default is to sleep.
//
// A heartbeat of 0 means the thread has not reported yet. That is startup,
// not a stall. A stage whose done flag is set has exited on purpose. The
// watchdog does not treat either as a fault.

#include <cstdint>
#include <cstdlib>

namespace ratehub {

struct Budget {
    bool overrun = false;
    std::int64_t sleep_ns = 0;
};

inline bool pacing_enabled() noexcept {
    const char* pace = std::getenv("RATEHUB_PACE");
    return pace == nullptr || pace[0] != '0';
}

inline Budget budget(std::int64_t work_ns, std::int64_t period_ns) noexcept {
    if (work_ns < 0) {
        work_ns = 0;
    }
    if (period_ns <= 0 || work_ns > period_ns) {
        return Budget{true, 0};
    }
    return Budget{false, period_ns - work_ns};
}

// True when the stage is still responsible for beating and the last beat is
// older than silence_ns. now and beat are the same monotonic clock.
inline bool heartbeat_stalled(std::uint64_t now, std::uint64_t beat, std::uint64_t silence_ns, bool stage_done) noexcept {
    if (stage_done || beat == 0 || now < beat) {
        return false;
    }
    return now - beat > silence_ns;
}

}  // namespace ratehub
