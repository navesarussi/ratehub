#include "ratehub/frame.hpp"
#include "ratehub/shm_control.hpp"

#include <cstdio>
#include <cstring>

namespace {

int g_failed = 0;

void expect(bool cond, const char* name) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s\n", name);
        ++g_failed;
    }
}

}  // namespace

int test_frame() {
    ratehub::Record in;
    in.source_id = 3;
    in.sequence = 9;
    in.time_ns = -15;
    in.pos_x = -1000;
    in.pos_y = 2000;
    in.vel_x = 50;
    in.vel_y = -4;

    std::uint8_t bytes[ratehub::kFrameBytes];
    expect(ratehub::encode_frame(in, bytes), "encode accepts source 3");
    expect(bytes[0] == 0xA1 && bytes[1] == 0x5C, "magic is big-endian");

    ratehub::Record out;
    out.pos_x = 42;
    expect(ratehub::decode_frame(bytes, ratehub::kFrameBytes, out) == ratehub::ParseStatus::Ok, "decode ok");
    expect(out.source_id == 3 && out.sequence == 9 && out.time_ns == -15, "identity fields");
    expect(out.pos_x == -1000 && out.pos_y == 2000 && out.vel_x == 50 && out.vel_y == -4, "values");

    bytes[36] ^= 0xFF;
    out.pos_x = 7;
    expect(ratehub::decode_frame(bytes, ratehub::kFrameBytes, out) == ratehub::ParseStatus::BadChecksum, "bad crc");
    expect(out.pos_x == 7, "failed decode does not write");

    in.source_id = 64;
    expect(!ratehub::encode_frame(in, bytes), "source 64 rejected");
    expect(ratehub::decode_frame(bytes, 4, out) == ratehub::ParseStatus::Truncated, "short frame");

    ratehub::ShmControl control;
    expect(control.magic == ratehub::kShmMagic, "shm magic");
    expect(control.schema == ratehub::kSchemaVersion, "shm schema");
    expect(control.generation.load(std::memory_order_relaxed) == 1u, "generation starts at 1");
    return g_failed;
}
