#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

#include <ao/pack/Error.h>

namespace ao::pack::bit {

struct BitPosition {
    size_t bitPos;

    size_t byteIndex() const { return bitPos / 8; }
    size_t bitIndex() const { return bitPos % 8; }
    bool aligned() const { return bitIndex() == 0; }
};

class ReadStream {
   public:
    ReadStream(std::span<std::byte> data) : m_data(data) {}
    ReadStream& align();
    ReadStream& bits(uint64_t& out, size_t count);

    // TODO maybe relax the alignment requirements of this?
    ReadStream& bytes(std::span<std::byte const>& out, size_t count);
    ReadStream& require(bool condition, Error err);

    size_t remainingBits() const;
    size_t remainingBytes() const { return remainingBits() / 8; }

    bool ok() const { return m_status == Error::Ok; }
    Error error() const { return m_status; }

   private:
    ReadStream& fail(Error err) {
        m_status = err;
        return *this;
    }
    bool readBit(std::byte& byte);

    Error m_status = Error::Ok;
    BitPosition m_position = {0};
    std::span<std::byte const> m_data;
};

class WriteStream {
   public:
    WriteStream(std::span<std::byte> buffer) : m_buffer(buffer) {}
    WriteStream& align();
    WriteStream& bits(uint64_t ingest, size_t count);
    WriteStream& bytes(std::span<std::byte> out, size_t count);
    WriteStream& require(bool condition, Error err);

    // Bits remaining in current buffer
    size_t remainingBits() const;
    size_t remainingBytes() const { return remainingBits() / 8; }

    bool ok() const { return m_status == Error::Ok; }
    Error error() const { return m_status; }

    size_t bitSize() const;
    size_t byteSize() const;

   private:
    WriteStream& fail(Error err) {
        m_status = err;
        return *this;
    }
    bool writeBit(std::byte byte);

    Error m_status = Error::Ok;
    BitPosition m_position = {0};
    std::span<std::byte> m_buffer;
};

class SizeWriteStream {
   public:
    SizeWriteStream& align();
    SizeWriteStream& bits(uint64_t ingest, size_t count);
    SizeWriteStream& bytes(std::span<std::byte> out, size_t count);
    SizeWriteStream& require(bool condition, Error err);

    // Bits remaining in current buffer
    size_t remainingBits() const;
    size_t remainingBytes() const { return remainingBits() / 8; }

    bool ok() const { return m_status == Error::Ok; }
    Error error() const { return m_status; }

    size_t bitSize() const;
    size_t byteSize() const;

   private:
    SizeWriteStream& fail(Error err) {
        m_status = err;
        return *this;
    }
    bool writeBit(std::byte byte);

    Error m_status = Error::Ok;
    BitPosition m_position = {0};
};

}  // namespace ao::pack::bit
