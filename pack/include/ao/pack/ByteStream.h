#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <ao/pack/Error.h>

namespace ao::pack::byte {
class ReadStream {
   public:
    ReadStream(std::span<std::byte const> data) : m_data(data) {}
    ReadStream& bytes(std::span<std::byte const>& out, size_t count);
    ReadStream& require(bool condition, Error err);

    size_t remainingBytes() const { return m_data.size() - m_position; }

    bool ok() const { return m_status == Error::Ok; }
    Error error() const { return m_status; }

   private:
    ReadStream& fail(Error err) {
        m_status = err;
        return *this;
    }

    Error m_status = Error::Ok;
    size_t m_position = 0;
    std::span<std::byte const> m_data;
};

class SizeWriteStream {
   public:
    SizeWriteStream& bytes(std::span<std::byte> data, size_t count) {
        if (!ok())
            return *this;
        if (remainingBytes() < count) {
            m_status = Error::Overflow;
            return *this;
        }

        m_position += count;
        return *this;
    }
    SizeWriteStream& require(bool condition, Error err) {
        if (!ok())
            return *this;
        if (!condition)
            m_status = err;
        return *this;
    }

    size_t remainingBytes() const {
        return std::numeric_limits<size_t>::max() - m_position;
    }

    bool ok() const { return m_status == Error::Ok; }
    Error error() const { return m_status; }

    size_t size() const { return m_position; }

   private:
    Error m_status = Error::Ok;
    size_t m_position = 0;
};

class WriteStream {
   public:
    WriteStream(std::span<std::byte> data) : m_data(data) {}
    WriteStream& bytes(std::span<std::byte> data, size_t count);
    WriteStream& require(bool condition, Error err);

    size_t remainingBytes() const { return m_data.size() - m_position; }

    bool ok() const { return m_status == Error::Ok; }
    Error error() const { return m_status; }

   private:
    WriteStream& fail(Error err) {
        m_status = err;
        return *this;
    }

    Error m_status = Error::Ok;
    size_t m_position = 0;
    std::span<std::byte> m_data;
};

}  // namespace ao::pack::byte
