#include "ratehub/frame.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

// Writes a deterministic replay file.
// Default: two frames for source 1 (unit tests).
// --demo: eight sources, 500 ticks (~20 s with ingest pacing).
int main(int argc, char** argv) {
    const char* path = "replay.bin";
    bool demo = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--demo") == 0) {
            demo = true;
        } else {
            path = argv[i];
        }
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::perror(path);
        return 1;
    }
    if (!demo) {
        ratehub::Record record;
        record.source_id = 1;
        record.pos_x = 1000;
        record.vel_x = 2000;
        for (std::uint32_t seq = 1; seq <= 2; ++seq) {
            record.sequence = seq;
            std::uint8_t frame[ratehub::kFrameBytes];
            if (!ratehub::encode_frame(record, frame)) {
                std::fprintf(stderr, "encode failed\n");
                return 1;
            }
            out.write(reinterpret_cast<char*>(frame), static_cast<std::streamsize>(ratehub::kFrameBytes));
        }
        std::printf("wrote %s (2 frames, source 1, vel 2000 mm/s)\n", path);
        return 0;
    }
    constexpr int kSources = 8;
    constexpr int kTicks = 500;
    int frames = 0;
    for (std::uint32_t seq = 1; seq <= kTicks; ++seq) {
        for (std::uint32_t id = 0; id < kSources; ++id) {
            ratehub::Record record;
            record.source_id = id;
            record.sequence = seq;
            record.pos_x = static_cast<std::int32_t>(id) * 800 - 2800;
            record.pos_y = static_cast<std::int32_t>(id % 2) * 1500 - 750;
            record.vel_x = static_cast<std::int32_t>(400 + id * 80);
            record.vel_y = static_cast<std::int32_t>(((id & 1u) != 0) ? (280 + id * 30) : -(280 + id * 30));
            std::uint8_t frame[ratehub::kFrameBytes];
            if (!ratehub::encode_frame(record, frame)) {
                std::fprintf(stderr, "encode failed\n");
                return 1;
            }
            out.write(reinterpret_cast<char*>(frame), static_cast<std::streamsize>(ratehub::kFrameBytes));
            ++frames;
        }
    }
    std::printf("wrote %s (%d frames, %d sources, %d ticks)\n", path, frames, kSources, kTicks);
    return 0;
}
