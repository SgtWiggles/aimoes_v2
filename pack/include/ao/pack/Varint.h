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
    uint64_t iterCount = 0;
    do {
        std::span<std::byte const> read;
        enc.bytes(read, 1);
        if (!enc.ok())
            return false;

        byte = (uint8_t)read[0];
        result |= (uint64_t(byte & 0x7f) << shift);
        shift += 7;
        ++iterCount;
    } while ((byte & 0x80) != 0 && iterCount <= 10);

    enc.require(iterCount <= 10, Error::BadData);
    return enc.ok();
}

inline std::tuple<uint8_t, uint8_t, uint8_t> encodePrefixIntHeader(uint64_t v) {
    uint8_t header = 0;

    int64_t headerBits = 7;
    uint64_t headerCapacity = 0x7F;
    uint8_t extraBytes = 0;
    uint64_t byteCapacity = 0;

    while ((headerCapacity | (byteCapacity << headerBits)) < v) {
        headerBits = std::max(headerBits - 1, 0LL);
        headerCapacity >>= 1;
        header >>= 1;
        header |= 0x80;

        ++extraBytes;
        byteCapacity <<= 8;
        byteCapacity |= 0xFF;
    }

    header |= (v & headerCapacity);
    return {header, headerBits, extraBytes};
}
inline std::tuple<uint8_t, uint8_t, uint64_t> decodePrefixIntHeader(
    uint8_t header) {
    auto extraBytes = std::countl_one(header);
    auto const headerBitCnt = (uint64_t)std::min(extraBytes + 1, 8);
    auto const headerNumberBits = (8 - headerBitCnt);
    auto mask = ~(~0ull << headerNumberBits);
    uint64_t v = header & mask;
    return {extraBytes, headerNumberBits, v};
}

template <class WriteStream>
bool encodePrefixInt(WriteStream& enc, uint64_t v) {
    auto [header, shift, extraBytes] = encodePrefixIntHeader(v);
    v >>= shift;
    auto rest = std::span<std::byte>{(std::byte*)&v, extraBytes};

    std::array<std::byte, sizeof(v) * 8 / 7 + 1> buffer = {};
    std::fill(buffer.begin(), buffer.end(), std::byte(0));
    buffer[0] = (std::byte)header;
    std::copy(rest.begin(), rest.end(), buffer.begin() + 1);
    return enc.bytes(std::span<std::byte>{buffer}, extraBytes + 1).ok();
}

template <class ReadStream>
bool decodePrefixInt(ReadStream& enc, uint64_t& out) {
    auto buf = std::span<std::byte const>{};
    if (!enc.bytes(buf, 1).ok())
        return false;
    auto [extra, shift, base] = decodePrefixIntHeader((uint8_t)buf[0]);
    if (!enc.bytes(buf, extra).ok())
        return false;

    auto rest = std::span<std::byte>{(std::byte*)&out, sizeof(uint64_t)};
    std::copy(buf.begin(), buf.end(), rest.begin());
    out = ((out << shift) | base);
    return true;
}

}  // namespace ao::pack
