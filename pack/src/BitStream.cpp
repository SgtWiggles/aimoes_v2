#include <ao/pack/BitStream.h>

#include <limits>

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
        std::span<std::byte const> b;
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

ReadStream& ReadStream::bytes(std::span<std::byte const>& out, size_t count) {
    if (!ok())
        return *this;
    if (count == 0)
        return *this;
    if (!m_position.aligned())
        return fail(Error::Unaligned);
    if (m_position.byteIndex() + count > m_data.size())
        return fail(Error::Eof);

    out = m_data.subspan(m_position.byteIndex(), count);
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
    if (m_position.bitIndex() != 0)
        return fail(Error::Unaligned);
    if (m_buffer.size() <= m_position.byteIndex())
        return fail(Error::Overflow);
    if (m_buffer.size() - m_position.byteIndex() < count)
        return fail(Error::Overflow);

    auto tmp = out.subspan(0, count);
    std::copy(tmp.begin(), tmp.end(),
              m_buffer.begin() + m_position.byteIndex());
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
    if (m_position.bitIndex() != 0)
        return fail(Error::Unaligned);
    if (std::numeric_limits<size_t>::max() - m_position.byteIndex() < count)
        return fail(Error::Overflow);

    m_position.bitPos += count * 8;
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
