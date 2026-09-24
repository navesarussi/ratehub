#pragma once

// Seqlock — latest-value publication for one writer and one reader.
//
// Contract
//   publish() makes one complete T visible. load() either copies that T, or
//   an older complete T, or returns false. It never returns a mix of two
//   publishes. On false, `out` is unchanged and the caller keeps its previous
//   sample.
//
// Sequence
//   Even means the payload is stable. Odd means a publish is in progress.
//   publish() stores odd, writes every word, then stores even. The two steps
//   advance the counter by 2, so a torn read sees begin != end and retries.
//   The counter is uint32. A reader that is preempted across 2^31 publishes
//   can mistake a new even value for the old one. That is accepted: a 5 ms
//   publisher would have to run for years. Do not use this for a counter a
//   reader can stall across.
//
// Memory order
//   Payload words are relaxed atomics. A plain T copied while the writer
//   stores it would be a data race in the C++ model, even when the sequence
//   check would discard the copy. Relaxed atomics make each word race-free.
//   They do not order the words with each other.
//   The release fence after the odd store stops the payload stores from
//   floating above the odd store. The release store of the even value stops
//   them from floating below the even store. The reader's acquire load of
//   `begin`, plus the acquire fence before `end`, makes a matching pair
//   observe those payload stores. seq_cst is not used. One writer and one
//   reader do not need a total order with the rest of the program.
//
// Alignment
//   The object is aligned to 64 bytes so two slots in an array do not share
//   a cache line. The writer dirties this line. A neighbor slot stays clean.
//   This is necessary sharing of one slot, not false sharing across slots.
//
// Failure
//   load() tries 64 times. A writer stuck between the odd store and the even
//   store makes every attempt see an odd sequence, and load() returns false.
//   That is a stuck writer, not a reason to spin forever on the reader.
//
// Not safe
//   Two publishers, two loaders, or a T that is not trivially copyable.
//   publish() from the thread that calls load() is a logic bug: the sequence
//   still "works", but it is no longer a handoff.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ratehub {

template <typename T>
class alignas(64) Seqlock {
    static_assert(std::is_trivially_copyable_v<T>, "seqlock payload must be trivially copyable");

public:
    // Copies `value` into the slot. The even store is the publication edge:
    // a load() that observes it sees every word of this value.
    void publish(const T& value) noexcept {
        Raw raw{};
        std::memcpy(raw.bytes, &value, sizeof(T));
        const std::uint32_t seq = seq_.load(std::memory_order_relaxed);
        seq_.store(seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        for (std::size_t i = 0; i < kWords; ++i) {
            words_[i].store(raw.words[i], std::memory_order_relaxed);
        }
        seq_.store(seq + 2, std::memory_order_release);
    }

    // True and `out` written only when begin == end and begin is even.
    bool load(T& out) const noexcept {
        for (int attempt = 0; attempt < 64; ++attempt) {
            const std::uint32_t begin = seq_.load(std::memory_order_acquire);
            if ((begin & 1u) != 0) {
                continue;
            }
            Raw raw{};
            for (std::size_t i = 0; i < kWords; ++i) {
                raw.words[i] = words_[i].load(std::memory_order_relaxed);
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            const std::uint32_t end = seq_.load(std::memory_order_relaxed);
            if (begin == end) {
                std::memcpy(&out, raw.bytes, sizeof(T));
                return true;
            }
        }
        return false;
    }

private:
    // Round up so the last partial word is still an atomic store.
    static constexpr std::size_t kWords = (sizeof(T) + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);

    union Raw {
        std::uint64_t words[kWords]{};
        unsigned char bytes[kWords * sizeof(std::uint64_t)];
    };

    std::atomic<std::uint32_t> seq_{0};
    std::atomic<std::uint64_t> words_[kWords]{};
};

}  // namespace ratehub
