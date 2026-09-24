#pragma once

// TripleBuffer — latest complete snapshot, one writer, one reader.
//
// Why three slots
//   A seqlock retries while the writer is in the middle of a large copy.
//   A fleet snapshot is the whole table. The reader should copy a slot the
//   writer will not touch. Three slots give the writer a private back buffer,
//   the reader a private front buffer, and one spare in the middle.
//
// Ownership at construction
//   write_ = 0, the writer fills slots_[0].
//   read_  = 1, the reader is allowed to read slots_[1] (still empty).
//   middle_ = 2, the spare, fresh bit clear.
//
// publish()
//   Exchanges middle_ with (write_ | fresh). The writer's slot becomes the
//   spare the reader may adopt. The index that came back, masked, is the
//   writer's next slot. The writer never receives the reader's current index
//   until the reader puts it into middle_ by consuming. The two sides
//   therefore do not own the same slot.
//
// consume()
//   If the fresh bit is clear, returns false and leaves read_ alone.
//   Otherwise exchanges middle_ with the bare read index (fresh cleared) and
//   adopts the index that was in the middle. A publish that lands between
//   the fresh load and the exchange is still safe: exchange returns whatever
//   index is there, and that index was finished by the writer.
//
// Missed snapshots
//   Two publish() calls before one consume() recycle the unread snapshot.
//   The reader observes the later one. This is latest-value, not a queue.
//   A slow reader cannot block the writer. Gaps are the caller's problem
//   (the generation counter on the shared mapping).
//
// Order
//   The exchange is acq_rel. The writer's stores to the slot happen-before
//   the release half. The reader's acquire half happens-before its loads of
//   that slot. No lock, no seq_cst.
//
// Not safe
//   A second writer or a second consumer. T must be trivially copyable if the
//   caller copies the slot out. The reference from read_slot() is valid only
//   until the next consume() on this object.

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace ratehub {

template <typename T>
class TripleBuffer {
    static_assert(std::is_trivially_copyable_v<T>, "snapshot type must be trivially copyable");

public:
    TripleBuffer() noexcept { middle_.store(2u, std::memory_order_relaxed); }

    // Slot the writer may fill. Invalid after publish() until the next call.
    T& write_slot() noexcept { return slots_[write_]; }

    // Slot from the last successful consume(). Empty T before the first one.
    const T& read_slot() const noexcept { return slots_[read_]; }

    void publish() noexcept {
        const unsigned old = middle_.exchange(static_cast<unsigned>(write_) | kFresh, std::memory_order_acq_rel);
        write_ = old & kIndexMask;
    }

    // False when no snapshot has been published since the last consume.
    bool consume() noexcept {
        if ((middle_.load(std::memory_order_acquire) & kFresh) == 0) {
            return false;
        }
        const unsigned old = middle_.exchange(static_cast<unsigned>(read_), std::memory_order_acq_rel);
        read_ = old & kIndexMask;
        return true;
    }

private:
    static constexpr unsigned kIndexMask = 0x3;
    static constexpr unsigned kFresh = 0x4;

    T slots_[3]{};
    std::size_t write_ = 0;
    std::size_t read_ = 1;
    std::atomic<unsigned> middle_{2};
};

}  // namespace ratehub
