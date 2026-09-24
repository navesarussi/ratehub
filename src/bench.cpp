#include "ratehub/types.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

// Not a CI gate. Warm up, then report the median of five runs.
// The sink keeps the compiler from deleting the loop.

namespace {

template <typename Fn>
long long median_ns(Fn&& fn) {
    long long samples[5];
    fn();
    for (long long& sample : samples) {
        const auto start = std::chrono::steady_clock::now();
        fn();
        sample = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    }
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            if (samples[j] < samples[i]) {
                const long long tmp = samples[i];
                samples[i] = samples[j];
                samples[j] = tmp;
            }
        }
    }
    return samples[2];
}

// Same element count. The contiguous int32 array is 2 MiB and fits in a
// 16 MiB L2. The strided array puts one int32 on each 64-byte line, so the
// same count occupies 32 MiB and does not. The product table of 64 sources
// is about 1 KB and is not used here.
constexpr std::size_t kLargeCount = (32u << 20) / 64u;

struct Line {
    std::int32_t x;
    unsigned char pad[60];
};

// noinline: if this is inlined into main, the compiler sees xs[i] == i and
// replaces the walk with a formula. The measurement would not touch memory.
[[gnu::noinline]] std::uint64_t sum_soa(const std::int32_t* xs, std::size_t count) {
    std::uint64_t sum = 0;
    for (int pass = 0; pass < 32; ++pass) {
        // Four independent sums. One accumulator waits on the add, and the
        // memory reference is hidden behind that latency.
        std::uint64_t a = 0;
        std::uint64_t b = 0;
        std::uint64_t c = 0;
        std::uint64_t d = 0;
        std::size_t i = 0;
        for (; i + 4 <= count; i += 4) {
            a += static_cast<std::uint32_t>(xs[i]);
            b += static_cast<std::uint32_t>(xs[i + 1]);
            c += static_cast<std::uint32_t>(xs[i + 2]);
            d += static_cast<std::uint32_t>(xs[i + 3]);
        }
        for (; i < count; ++i) {
            a += static_cast<std::uint32_t>(xs[i]);
        }
        sum += a + b + c + d;
        asm volatile("" : "+r"(sum)::"memory");
    }
    return sum;
}

[[gnu::noinline]] std::uint64_t sum_aos(const Line* points, std::size_t count) {
    std::uint64_t sum = 0;
    for (int pass = 0; pass < 32; ++pass) {
        std::uint64_t a = 0;
        std::uint64_t b = 0;
        std::uint64_t c = 0;
        std::uint64_t d = 0;
        std::size_t i = 0;
        for (; i + 4 <= count; i += 4) {
            a += static_cast<std::uint32_t>(points[i].x);
            b += static_cast<std::uint32_t>(points[i + 1].x);
            c += static_cast<std::uint32_t>(points[i + 2].x);
            d += static_cast<std::uint32_t>(points[i + 3].x);
        }
        for (; i < count; ++i) {
            a += static_cast<std::uint32_t>(points[i].x);
        }
        sum += a + b + c + d;
        asm volatile("" : "+r"(sum)::"memory");
    }
    return sum;
}

void pound(std::atomic<std::uint64_t>& left, std::atomic<std::uint64_t>& right) {
    std::thread a([&] {
        for (int i = 0; i < 200000; ++i) {
            left.fetch_add(1, std::memory_order_relaxed);
        }
    });
    std::thread b([&] {
        for (int i = 0; i < 200000; ++i) {
            right.fetch_add(1, std::memory_order_relaxed);
        }
    });
    a.join();
    b.join();
}

}  // namespace

int main() {
    std::vector<std::int32_t> xs(kLargeCount);
    std::vector<Line> points(kLargeCount);
    for (std::size_t i = 0; i < kLargeCount; ++i) {
        xs[i] = static_cast<std::int32_t>(i);
        points[i].x = xs[i];
    }
    std::uint64_t sink = 0;
    const long long soa = median_ns([&] { sink += sum_soa(xs.data(), xs.size()); });
    const long long aos = median_ns([&] { sink += sum_aos(points.data(), points.size()); });

    struct Adjacent {
        std::atomic<std::uint64_t> left{0};
        std::atomic<std::uint64_t> right{0};
    } adjacent;
    struct Padded {
        alignas(64) std::atomic<std::uint64_t> left{0};
        alignas(64) std::atomic<std::uint64_t> right{0};
    } padded;
    const long long shared = median_ns([&] { pound(adjacent.left, adjacent.right); });
    const long long apart = median_ns([&] { pound(padded.left, padded.right); });

    std::cout << "soa_ns " << soa << "\naos_ns " << aos << "\nadjacent_ns " << shared << "\npadded_ns " << apart
              << "\nsink " << sink << '\n';
    return sink == 0 ? 1 : 0;
}
