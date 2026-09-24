#pragma once

// PIDs and exits are written by the supervisor. Observer acquire-loads them.
// rerun_request is stored by the observer (POST /reset) and consumed by the
// supervisor. observe_release is 0 while the observer may serve HTTP.

#include <atomic>
#include <cstdint>

namespace ratehub {

struct alignas(64) SupervisorState {
    std::atomic<std::uint32_t> ingest_pid{0};
    std::atomic<std::uint32_t> compute_pid{0};
    std::atomic<std::uint32_t> publish_pid{0};
    std::atomic<std::uint32_t> observe_pid{0};
    std::atomic<std::uint32_t> ingest_exit{0};
    std::atomic<std::uint32_t> compute_exit{0};
    std::atomic<std::uint32_t> publish_exit{0};
    std::atomic<std::uint32_t> observe_release{0};
    // Observer may store 1. Supervisor exchange-clears it to start another run.
    std::atomic<std::uint32_t> rerun_request{0};
};

inline void request_rerun(SupervisorState& state) noexcept {
    state.rerun_request.store(1, std::memory_order_release);
}

inline bool take_rerun(SupervisorState& state) noexcept {
    return state.rerun_request.exchange(0, std::memory_order_acq_rel) != 0;
}

}  // namespace ratehub
