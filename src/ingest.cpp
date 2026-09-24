#include "ratehub/roles.hpp"

#include "ratehub/bounded_queue.hpp"
#include "ratehub/frame.hpp"
#include "ratehub/shm_region.hpp"

#include <array>
#include <atomic>
#include <fstream>
#include <thread>

namespace ratehub {
namespace {

bool stopped(const ShmControl& control) {
    return control.shutdown.load(std::memory_order_acquire) != 0;
}

bool push_record(Layout& layout, const Record& record) {
    while (!layout.ring.try_push(record)) {
        if (stopped(layout.control)) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

}  // namespace

int run_ingest(const char* shm_name, const char* replay_path) {
    ShmMapping mapping;
    if (shm_open_existing(shm_name, mapping) != ShmStatus::Ok) {
        return 1;
    }
    Layout& layout = *mapping.layout;
    BoundedQueue<std::array<std::uint8_t, kFrameBytes>, 64> queue;
    std::atomic<std::uint64_t> beats{0};

    std::thread source([&] {
        std::ifstream in(replay_path, std::ios::binary);
        std::array<std::uint8_t, kFrameBytes> frame{};
        while (in.read(reinterpret_cast<char*>(frame.data()), static_cast<std::streamsize>(frame.size()))) {
            if (stopped(layout.control) || !queue.push(frame)) {
                break;
            }
            layout.control.heartbeat_ingest.store(++beats, std::memory_order_release);
        }
        queue.close();
    });

    std::thread frames([&] {
        for (;;) {
            std::optional<std::array<std::uint8_t, kFrameBytes>> frame = queue.pop();
            if (!frame.has_value()) {
                break;
            }
            Record record;
            if (decode_frame(frame->data(), frame->size(), record) != ParseStatus::Ok) {
                continue;
            }
            if (!push_record(layout, record)) {
                break;
            }
        }
    });

    source.join();
    frames.join();
    layout.control.ingest_done.store(1, std::memory_order_release);
    shm_close(mapping);
    return 0;
}

}  // namespace ratehub
