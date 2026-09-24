#include "ratehub/bounded_queue.hpp"
#include "ratehub/seqlock.hpp"
#include "ratehub/spsc_ring.hpp"
#include "ratehub/triple_buffer.hpp"

#include <cstdio>
#include <thread>

namespace {

int g_failed = 0;

void expect(bool cond, const char* name) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s\n", name);
        ++g_failed;
    }
}

}  // namespace

int test_publish() {
    ratehub::SpscRing<int, 4> ring;
    expect(ring.try_push(1) && ring.try_push(2), "ring push");
    int value = 0;
    expect(ring.try_pop(value) && value == 1, "ring fifo");
    expect(ring.try_push(3) && ring.try_push(4), "ring fill");
    expect(!ring.try_push(5), "ring full");

    ratehub::Seqlock<std::uint64_t> slot;
    slot.publish(0x1122334455667788ULL);
    std::uint64_t got = 0;
    expect(slot.load(got) && got == 0x1122334455667788ULL, "seqlock roundtrip");

    std::thread writer([&slot] {
        for (std::uint64_t i = 0; i < 10000; ++i) {
            slot.publish(i);
        }
    });
    std::uint64_t prev = 0;
    bool ordered = true;
    while (prev < 9999) {
        std::uint64_t sample = 0;
        if (!slot.load(sample)) {
            continue;
        }
        if (sample < prev) {
            ordered = false;
            break;
        }
        prev = sample;
    }
    writer.join();
    expect(ordered, "seqlock values do not go backwards");

    ratehub::TripleBuffer<int> buffer;
    expect(!buffer.consume(), "no snapshot yet");
    buffer.write_slot() = 7;
    buffer.publish();
    buffer.write_slot() = 8;
    buffer.publish();
    expect(buffer.consume() && buffer.read_slot() == 8, "reader sees the latest complete slot");

    ratehub::BoundedQueue<int, 1> queue;
    expect(queue.push(4), "queue push");
    std::thread blocked([&queue] { queue.push(5); });
    queue.close();
    blocked.join();
    expect(queue.pop() == 4, "queued value still pops");
    expect(!queue.pop().has_value(), "closed empty queue returns null");
    return g_failed;
}
