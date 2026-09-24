#include "ratehub/roles.hpp"

#include "ratehub/shm_region.hpp"

#include <fstream>
#include <thread>

namespace ratehub {

int run_publish(const char* shm_name, const char* out_path) {
    ShmMapping mapping;
    if (shm_open_existing(shm_name, mapping) != ShmStatus::Ok) {
        return 1;
    }
    Layout& layout = *mapping.layout;
    Snapshot last{};
    bool have = false;
    std::uint64_t beats = 0;
    while (layout.control.shutdown.load(std::memory_order_acquire) == 0) {
        if (layout.snapshots.consume()) {
            last = layout.snapshots.read_slot();
            have = true;
        }
        layout.control.heartbeat_publish.store(++beats, std::memory_order_release);
        if (layout.control.compute_done.load(std::memory_order_acquire) != 0 && have) {
            break;
        }
        std::this_thread::yield();
    }
    int rc = 0;
    if (have) {
        std::ofstream out(out_path);
        if (!out) {
            rc = 1;
        } else {
            out << "generation " << last.generation << '\n';
            for (std::uint32_t id = 0; id < kMaxSources; ++id) {
                if (last.rows[id].publishes == 0) {
                    continue;
                }
                out << id << ' ' << last.rows[id].pos_x << ' ' << last.rows[id].pos_y << ' '
                    << last.rows[id].publishes << '\n';
            }
        }
    }
    shm_close(mapping);
    return rc;
}

}  // namespace ratehub
