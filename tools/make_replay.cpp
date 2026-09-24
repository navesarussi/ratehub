#include "ratehub/frame.hpp"

#include <cstdio>
#include <fstream>

// Writes a deterministic replay file: two frames for source 1, 5 ms apart in
// the integrator sense (the replay itself is not paced).
int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "replay.bin";
    ratehub::Record record;
    record.source_id = 1;
    record.pos_x = 1000;
    record.vel_x = 2000;
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::perror(path);
        return 1;
    }
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
