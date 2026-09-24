#pragma once

// Wire frame. Big-endian. 38 bytes. CRC-16/CCITT-FALSE over the first 36.
//
// Offset  Size  Field
// 0       2     magic, must be kFrameMagic (0xA15C)
// 2       2     schema, must be kSchemaVersion
// 4       2     source id, must be < kMaxSources
// 6       2     reserved, writers store 0, readers ignore
// 8       4     sequence
// 12      8     time_ns, two's complement, big-endian
// 20      4     pos_x millimeters
// 24      4     pos_y
// 28      4     vel_x millimeters per second
// 32      4     vel_y
// 36      2     CRC over bytes [0, 36)
//
// The reserved half-word keeps the sequence on a 4-byte offset in a dump.
// It is not padding inside a C struct. The wire image is written byte by
// byte. A struct overlay would depend on endianness, padding, and the
// strict-aliasing rule. decode_frame memcpy's into a local array and then
// shifts bytes into a native Record.
//
// Failure does not write the output Record. A bad checksum is dropped and
// counted. It is not retried. The next frame is independent.

#include "ratehub/types.hpp"

#include <cstddef>
#include <cstdint>

namespace ratehub {

// Polynomial 0x1021, initial value 0xFFFF, no final xor. `data` may be null
// only when length is 0.
std::uint16_t frame_crc(const std::uint8_t* data, std::size_t length) noexcept;

// Writes kFrameBytes to `out`. False when source_id >= kMaxSources; `out`
// is then untouched. True means the CRC matches what decode_frame expects.
bool encode_frame(const Record& record, std::uint8_t out[kFrameBytes]) noexcept;

// Ok copies one record into `out`. Every other status leaves `out` unchanged.
// `bytes == nullptr` or `length < kFrameBytes` is Truncated.
ParseStatus decode_frame(const std::uint8_t* bytes, std::size_t length, Record& out) noexcept;

}  // namespace ratehub
