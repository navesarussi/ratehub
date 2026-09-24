#pragma once

// BoundedQueue — mutex queue for the ingest cold path.
//
// Where it sits
//   The source thread reads bytes. The frame thread decodes. This queue is
//   the only mutex on that path. The 5 ms loop does not take it. A lock there
//   would couple decode latency to the rate thread.
//
// Capacity
//   push() waits while size_ == Capacity, unless the queue is closed.
//   Waiting is the backpressure. The queue does not drop. pop() waits while
//   empty, unless closed.
//
// close()
//   Sets closed_ under the mutex and notify_all(). A push() that wakes
//   because of close returns false and does not store. A pop() that wakes
//   still returns queued items first. When the queue is empty and closed,
//   pop() returns nullopt. One notify_one() would leave the other waiter
//   asleep. Both sides may be waiting, so the wake is broadcast.
//
// Shutdown
//   The supervisor's flag is observed by whichever thread owns the queue
//   side, and that thread calls close(). The queue does not read the flag
//   itself. Keeping the flag out of this class leaves the wait free of
//   shared-memory atomics.
//
// Exceptions
//   The element type must be nothrow-move-constructible, or trivially
//   copyable. push() and pop() move under the lock. A throw there would
//   skip the unlock only if we did not use unique_lock; unique_lock unlocks
//   on unwind, but size_ would already have changed. The type constraint
//   removes that window.
//
// Not the hot ring
//   SpscRing is the record handoff into compute. This queue carries bytes
//   or decoded records only between the two ingest threads.

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <type_traits>

namespace ratehub {

template <typename T, std::size_t Capacity>
class BoundedQueue {
    static_assert(Capacity > 0, "queue capacity must be positive");
    static_assert(std::is_nothrow_move_constructible_v<T> || std::is_trivially_copyable_v<T>,
                  "queue element must be movable without throwing");

public:
    // False only after close(). The value is not stored in that case.
    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return closed_ || size_ < Capacity; });
        if (closed_) {
            return false;
        }
        slots_[(head_ + size_) % Capacity] = std::move(value);
        ++size_;
        lock.unlock();
        ready_.notify_one();
        return true;
    }

    // nullopt when closed and empty. Otherwise the oldest element.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return closed_ || size_ > 0; });
        if (size_ == 0) {
            return std::nullopt;
        }
        T value = std::move(slots_[head_]);
        head_ = (head_ + 1) % Capacity;
        --size_;
        lock.unlock();
        ready_.notify_one();
        return value;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        ready_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    T slots_[Capacity]{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    bool closed_ = false;
};

}  // namespace ratehub
