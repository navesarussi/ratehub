#pragma once

// SpscRing — bounded queue for one producer thread and one consumer thread.
//
// Capacity
//   Capacity is a power of two so the index wraps with a mask. One slot is
//   left empty. Full and empty would otherwise look the same (write == read).
//   A ring of 4 holds 3 records. try_push returns false when the next write
//   index equals the read index. The caller blocks or counts backpressure.
//   This function itself does not wait and does not drop the new value.
//
// Publication
//   The producer writes slots_[write], then release-stores the advanced write
//   index. The consumer acquire-loads the write index, then reads the slot.
//   The payload store happens-before that release, which synchronizes-with
//   the consumer's acquire. The slot type is not atomic. The indexes are what
//   keep the two threads off the same slot. Two producers, or two consumers,
//   break that and are a data race.
//
// Indexes
//   Each side loads its own index with relaxed order. Only the other side's
//   index needs acquire, because that is the publication edge. write_ and
//   read_ are on separate 64-byte lines. If they shared a line, every push
//   would invalidate the consumer's line and the other way around. That is
//   false sharing: the threads touch different variables.
//
// Lifetime
//   T must be trivially copyable. try_push copies. There is no destructor
//   call that could race with a reader still looking at an older generation
//   of the same slot, because a slot is not reused until the consumer has
//   advanced past it.
//
// Shutdown
//   This ring has no close flag. The ingest path that must wake on shutdown
//   is BoundedQueue. The hot record ring stays non-blocking so the 5 ms
//   thread can count a miss and move on.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ratehub {

template <typename T, std::uint32_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "slot type must be trivially copyable");

public:
    // False means the ring is full. `value` was not stored.
    bool try_push(const T& value) noexcept {
        const std::uint32_t write = write_.load(std::memory_order_relaxed);
        const std::uint32_t next = (write + 1) & kMask;
        if (next == read_.load(std::memory_order_acquire)) {
            return false;
        }
        slots_[write] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }

    // False means the ring is empty. `out` is unchanged.
    bool try_pop(T& out) noexcept {
        const std::uint32_t read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) {
            return false;
        }
        out = slots_[read];
        read_.store((read + 1) & kMask, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::uint32_t kMask = Capacity - 1;

    T slots_[Capacity]{};
    alignas(64) std::atomic<std::uint32_t> write_{0};
    alignas(64) std::atomic<std::uint32_t> read_{0};
};

}  // namespace ratehub
