#include <ao/pack/BitStream.h>

#include <limits>
#include <algorithm>  // for std::copy and std::min

namespace ao::pack::bit {
ReadStream& ReadStream::align() {
    if (!ok())
        return *this;
    if (m_position.aligned())
        return *this;
    auto const skip = 8 - m_position.bitIndex();
    uint64_t skipData;
    bits(skipData, skip);
    return *this;
}

ReadStream& ReadStream::bits(uint64_t& out, size_t count) {
    if (!ok())
        return *this;
    if (count > 64)
        return fail(Error::BadArg);

    out = 0;
    size_t produced = 0;

    if (m_position.bitIndex() != 0 && count > 0) {
        auto const toAlign = 8 - m_position.bitIndex();
        auto const take = std::min(count, toAlign);

        std::byte b;
        for (size_t i = 0; i < take; ++i) {
            if (!readBit(b))
                return *this;
            out |= ((uint64_t)b << produced);
            ++produced;
        }

        count -= take;
    }

    while (ok() && count >= 8) {
        std::byte data;
        std::span<std::byte> b = {&data, 1};
        bytes(b, 1);
        if (!ok())
            return *this;
        out |= ((uint64_t)b[0] << produced);
        produced += 8;
        count -= 8;
    }

    for (size_t i = 0; i < count; ++i) {
        std::byte b;
        if (!readBit(b))
            return *this;
        out |= ((uint64_t)b << produced);
        ++produced;
    }
    return *this;
}

ReadStream& ReadStream::bytes(std::span<std::byte> out, size_t count) {
    if (!ok())
        return *this;
    if (count == 0)
        return *this;
    if (out.size() < count)
        return fail(Error::BadArg);

    const size_t startByte = m_position.byteIndex();
    const unsigned startBit = static_cast<unsigned>(m_position.bitIndex());

    // required source bytes (may need one extra when unaligned)
    const size_t requiredSrc = count + (startBit != 0 ? 1 : 0);
    if (startByte + requiredSrc > m_data.size())
        return fail(Error::Eof);

    // Aligned fast-path: copy directly into caller buffer.
    if (startBit == 0) {
        auto src = m_data.subspan(startByte, count);
        std::copy(src.begin(), src.end(), out.begin());
        m_position.bitPos += count * 8;
        return *this;
    }

    // Unaligned: assemble bytes by shifting blocks using a 64-bit word.
    const unsigned s = startBit;
    const size_t maxOutput = (64u - s) / 8u;  // typically 7 for s in 1..7

    size_t srcIdx = startByte;
    size_t dstIdx = 0;
    size_t remaining = count;

    while (remaining > 0) {
        const size_t k = std::min(maxOutput, remaining);

        // Pack (k + 1) source bytes into a 64-bit little-endian word.
        uint64_t u = 0;
        for (size_t i = 0; i < k + 1; ++i) {
            u |= (uint64_t)static_cast<uint8_t>(m_data[srcIdx + i]) << (8 * i);
        }

        // Shift right by the start-bit offset; lower bytes are the output bytes.
        uint64_t v = u >> s;

        for (size_t i = 0; i < k; ++i) {
            uint8_t byte = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
            out[dstIdx + i] = std::byte{byte};
        }

        srcIdx += k;
        dstIdx += k;
        remaining -= k;
    }

    m_position.bitPos += count * 8;
    return *this;
}

ReadStream& ReadStream::require(bool condition, Error err) {
    if (!ok())
        return *this;
    if (!condition)
        fail(err);
    return *this;
}

size_t ReadStream::remainingBits() const {
    if (m_position.bitPos >= (m_data.size() * 8))
        return 0;
    return (m_data.size() * 8) - m_position.bitPos;
}

bool ReadStream::readBit(std::byte& out) {
    if (!ok())
        return false;
    if (m_position.byteIndex() >= m_data.size()) {
        fail(Error::Eof);
        return false;
    }
    out = ((m_data[m_position.byteIndex()] >> m_position.bitIndex()) &
           std::byte{1});
    ++m_position.bitPos;
    return true;
}

WriteStream& WriteStream::align() {
    if (!ok())
        return *this;

    if (m_position.bitIndex() == 0)
        return *this;
    auto const toAdvance = 8 - m_position.bitIndex();
    for (size_t i = 0; i < toAdvance; ++i)
        writeBit(std::byte{0});
    return *this;
}

WriteStream& WriteStream::bits(uint64_t v, size_t count) {
    if (!ok())
        return *this;
    if (count > 64)
        return fail(Error::BadArg);

    while (m_position.bitIndex() != 0 && count > 0) {
        if (!writeBit(std::byte{v & 1}))
            return *this;
        v >>= 1;
        --count;
    }

    while (count >= 8) {
        auto b = std::byte{v & 0xFF};
        bytes({&b, 1}, 1);
        if (!ok())
            return *this;
        count -= 8;
        v >>= 8;
    }

    while (count > 0) {
        if (!writeBit(std::byte{v & 1}))
            return *this;
        v >>= 1;
        --count;
    }

    return *this;
}
WriteStream& WriteStream::bytes(std::span<std::byte> out, size_t count) {
    if (!ok())
        return *this;
    if (count == 0)
        return *this;
    if (out.size() < count)
        return fail(Error::BadArg);

    auto tmp = out.subspan(0, count);

    const size_t startByte = m_position.byteIndex();
    const unsigned startBit = static_cast<unsigned>(m_position.bitIndex());

    if (m_buffer.size() <= startByte)
        return fail(Error::Overflow);

    // required bytes = count bytes plus one extra destination byte when
    // unaligned
    const size_t requiredBytes = count + (startBit != 0 ? 1 : 0);
    if (m_buffer.size() - startByte < requiredBytes)
        return fail(Error::Overflow);

    // Aligned fast-path: direct copy
    if (startBit == 0) {
        std::copy(tmp.begin(), tmp.end(), m_buffer.begin() + startByte);
        m_position.bitPos += count * 8;
        return *this;
    }

    // Unaligned path: build little-endian words of up to 7 input bytes,
    // shift once into position, write the resulting bytes.
    const unsigned shift = startBit;
    const unsigned rshift = 8u - shift;
    const uint8_t lowMask = static_cast<uint8_t>(
        (1u << shift) - 1u);  // bits to preserve in low part
    const uint8_t highMask =
        static_cast<uint8_t>(~lowMask);  // bits to preserve in high part

    size_t dst = startByte;
    size_t srcIdx = 0;
    size_t remaining = count;

    // Max number of input bytes we can pack into a 64-bit word without
    // overflow:
    const size_t maxBlock = (64u - shift) / 8u;  // <= 7 for shift in 1..7

    while (remaining > 0) {
        const size_t k = std::min(maxBlock, remaining);

        // Pack k bytes little-endian into u
        uint64_t u = 0;
        for (size_t i = 0; i < k; ++i) {
            u |= (uint64_t)static_cast<uint8_t>(tmp[srcIdx + i]) << (8 * i);
        }

        // Shift into destination bit position
        uint64_t uLeft = u << shift;

        // byte 0: merge into current destination byte, preserve low bits
        uint8_t b0 = static_cast<uint8_t>(uLeft & 0xFFu);
        uint8_t cur0 = static_cast<uint8_t>(m_buffer[dst]);
        m_buffer[dst] = std::byte{static_cast<uint8_t>((cur0 & lowMask) | b0)};

        // middle bytes (fully covered by payload) - overwrite directly
        for (size_t i = 1; i < k; ++i) {
            uint8_t bi = static_cast<uint8_t>((uLeft >> (8 * i)) & 0xFFu);
            m_buffer[dst + i] = std::byte{bi};
        }

        // carry: write low bits into the next destination byte, preserve high
        // bits
        uint8_t bk = static_cast<uint8_t>((uLeft >> (8 * k)) & 0xFFu);
        uint8_t curCarry = static_cast<uint8_t>(m_buffer[dst + k]);
        m_buffer[dst + k] =
            std::byte{static_cast<uint8_t>((curCarry & highMask) | bk)};

        dst += k;
        srcIdx += k;
        remaining -= k;
    }

    m_position.bitPos += count * 8;
    return *this;
}
WriteStream& WriteStream::require(bool condition, Error err) {
    if (!ok())
        return *this;
    if (!condition)
        m_status = err;
    return *this;
}

size_t WriteStream::remainingBits() const {
    auto const bufBits = m_buffer.size() * 8;
    auto const posBits = m_position.bitPos;
    if (bufBits < posBits)
        return 0;
    return bufBits - posBits;
}

size_t WriteStream::bitSize() const {
    return m_position.bitPos;
}
size_t WriteStream::byteSize() const {
    if (m_position.bitIndex() == 0)
        return m_position.byteIndex();
    return m_position.byteIndex() + 1;
}

bool WriteStream::writeBit(std::byte byte) {
    if (!ok())
        return false;
    auto idx = m_position.byteIndex();
    if (idx >= m_buffer.size()) {
        fail(Error::Overflow);
        return false;
    }
    m_buffer[idx] &= ~std::byte(1 << m_position.bitIndex());
    m_buffer[idx] |= ((byte & std::byte(1)) << m_position.bitIndex());
    m_position.bitPos += 1;
    return true;
}

SizeWriteStream& SizeWriteStream::align() {
    if (!ok())
        return *this;

    if (m_position.bitIndex() == 0)
        return *this;
    auto const toAdvance = 8 - m_position.bitIndex();
    for (size_t i = 0; i < toAdvance; ++i)
        writeBit(std::byte{0});
    return *this;
}

SizeWriteStream& SizeWriteStream::bits(uint64_t v, size_t count) {
    if (!ok())
        return *this;
    if (count > 64)
        return fail(Error::BadArg);

    while (m_position.bitIndex() != 0 && count > 0) {
        if (!writeBit(std::byte{v & 1}))
            return *this;
        v >>= 1;
        --count;
    }

    while (count >= 8) {
        auto b = std::byte{v & 0xFF};
        bytes({&b, 1}, 1);
        if (!ok())
            return *this;
        count -= 8;
        v >>= 8;
    }

    while (count > 0) {
        if (!writeBit(std::byte{v & 1}))
            return *this;
        v >>= 1;
        --count;
    }

    return *this;
}
SizeWriteStream& SizeWriteStream::bytes(std::span<std::byte> out,
                                        size_t count) {
    if (!ok())
        return *this;
    if (out.size() < count)
        return fail(Error::BadArg);

    // Allow unaligned size-only byte requests; just ensure we don't overflow.
    if (count > std::numeric_limits<size_t>::max() / 8)
        return fail(Error::Overflow);
    const size_t bits = count * 8;
    if (std::numeric_limits<size_t>::max() - m_position.bitPos < bits)
        return fail(Error::Overflow);

    m_position.bitPos += bits;
    return *this;
}
SizeWriteStream& SizeWriteStream::require(bool condition, Error err) {
    if (!ok())
        return *this;
    if (!condition)
        m_status = err;
    return *this;
}

size_t SizeWriteStream::remainingBits() const {
    return std::numeric_limits<size_t>::max() - m_position.bitPos;
}

size_t SizeWriteStream::bitSize() const {
    return m_position.bitPos;
}
size_t SizeWriteStream::byteSize() const {
    if (m_position.bitIndex() == 0)
        return m_position.byteIndex();
    return m_position.byteIndex() + 1;
}

bool SizeWriteStream::writeBit(std::byte byte) {
    if (!ok())
        return false;
    m_position.bitPos += 1;
    return true;
}
}  // namespace ao::pack::bit
