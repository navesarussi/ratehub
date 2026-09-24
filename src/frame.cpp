#include "ratehub/frame.hpp"

#include <cstring>

// Byte stores, not a struct cast. The host may be little-endian. The wire
// is big-endian. Shifting into a local integer is defined. Casting the
// caller's buffer to uint32_t* would be an aliasing violation and, on some
// architectures, an unaligned fault.

namespace ratehub {
namespace {

void store_u16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v);
}

void store_u32(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

void store_u64(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int shift = 56; shift >= 0; shift -= 8) {
        *p++ = static_cast<std::uint8_t>(v >> shift);
    }
}

std::uint16_t load_u16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint32_t load_u32(const std::uint8_t* p) noexcept {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
           (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
}

std::uint64_t load_u64(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | p[i];
    }
    return v;
}

}  // namespace

std::uint16_t frame_crc(const std::uint8_t* data, std::size_t length) noexcept {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000) != 0) {
                crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<std::uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

bool encode_frame(const Record& record, std::uint8_t out[kFrameBytes]) noexcept {
    if (record.source_id >= kMaxSources) {
        return false;
    }
    store_u16(out + 0, kFrameMagic);
    store_u16(out + 2, kSchemaVersion);
    store_u16(out + 4, static_cast<std::uint16_t>(record.source_id));
    store_u16(out + 6, 0);
    store_u32(out + 8, record.sequence);
    store_u64(out + 12, static_cast<std::uint64_t>(record.time_ns));
    store_u32(out + 20, static_cast<std::uint32_t>(record.pos_x));
    store_u32(out + 24, static_cast<std::uint32_t>(record.pos_y));
    store_u32(out + 28, static_cast<std::uint32_t>(record.vel_x));
    store_u32(out + 32, static_cast<std::uint32_t>(record.vel_y));
    // Bytes 6..7 stay zero so a hex dump keeps the later fields on even
    // boundaries. The CRC covers everything before it.
    const std::uint16_t crc = frame_crc(out, 36);
    store_u16(out + 36, crc);
    return true;
}

ParseStatus decode_frame(const std::uint8_t* bytes, std::size_t length, Record& out) noexcept {
    if (bytes == nullptr || length < kFrameBytes) {
        return ParseStatus::Truncated;
    }
    std::uint8_t local[kFrameBytes];
    std::memcpy(local, bytes, kFrameBytes);
    if (load_u16(local + 0) != kFrameMagic) {
        return ParseStatus::BadMagic;
    }
    if (load_u16(local + 2) != kSchemaVersion) {
        return ParseStatus::BadVersion;
    }
    const std::uint16_t source = load_u16(local + 4);
    if (source >= kMaxSources) {
        return ParseStatus::BadSource;
    }
    const std::uint16_t expect = frame_crc(local, 36);
    if (load_u16(local + 36) != expect) {
        return ParseStatus::BadChecksum;
    }
    Record decoded;
    decoded.source_id = source;
    decoded.sequence = load_u32(local + 8);
    decoded.time_ns = static_cast<std::int64_t>(load_u64(local + 12));
    decoded.pos_x = static_cast<std::int32_t>(load_u32(local + 20));
    decoded.pos_y = static_cast<std::int32_t>(load_u32(local + 24));
    decoded.vel_x = static_cast<std::int32_t>(load_u32(local + 28));
    decoded.vel_y = static_cast<std::int32_t>(load_u32(local + 32));
    out = decoded;
    return ParseStatus::Ok;
}

}  // namespace ratehub
