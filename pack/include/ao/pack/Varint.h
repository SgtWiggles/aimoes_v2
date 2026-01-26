#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ao::pack {
template <class WriteStream>
bool encodeVarint(WriteStream& enc, uint64_t v) {
    std::array<std::byte, sizeof(v) * 8 / 7 + 1> buffer;
    std::byte* p = buffer.data();

    do {
        std::byte byte = std::byte(v & 0x7f);
        v >>= 7;
        if (v != 0)
            byte |= std::byte(0x80);
        *p = byte;
        ++p;
    } while (v != 0);

    enc.bytes(std::span<std::byte>{buffer.data(), p}, p - buffer.data());
    return enc.ok();
}

template <class ReadStream>
bool decodeVarint(ReadStream& enc, uint64_t& result) {
    result = 0;
    uint64_t shift = 0;
    uint8_t byte;
    do {
        std::span<std::byte const> read;
        enc.bytes(read, 1);
        if (!enc.ok())
            return false;

        byte = (uint8_t)read[0];
        result |= (uint64_t(byte & 0x7f) << shift);
        shift += 7;
    } while ((byte & 0x80) != 0);
    return enc.ok();
}

}  // namespace ao::pack
