#pragma once

#include <cstddef>
#include <limits>
#include <ostream>
#include <span>

#include <ao/pack/Error.h>

namespace ao::pack::byte {

// Write stream that writes bytes to a std::ostream. It exposes the same
// interface as ao::pack::byte::WriteStream but writes into an output stream
// rather than a fixed buffer. Because ostream doesn't have a fixed capacity
// remainingBytes() reports a very large number.
class OStreamWriteStream {
   public:
    explicit OStreamWriteStream(std::ostream& os) : m_os(&os) {}

    OStreamWriteStream& bytes(std::span<std::byte const> data, size_t count) {
        if (!ok())
            return *this;
        if (count == 0)
            return *this;
        if (data.size() < count)
            return fail(Error::BadArg);

        m_os->write(reinterpret_cast<char const*>(data.data()),
                    static_cast<std::streamsize>(count));
        if (m_os->fail())
            return fail(Error::Overflow);

        m_position += count;
        return *this;
    }

    OStreamWriteStream& require(bool condition, Error err) {
        if (!ok())
            return *this;
        if (!condition)
            m_status = err;
        return *this;
    }

    size_t remainingBytes() const { return std::numeric_limits<size_t>::max(); }

    bool ok() const { return m_status == Error::Ok; }
    Error error() const { return m_status; }

    size_t byteSize() const { return m_position; }

   private:
    OStreamWriteStream& fail(Error err) {
        m_status = err;
        return *this;
    }

    Error m_status = Error::Ok;
    size_t m_position = 0;
    std::ostream* m_os = nullptr;
};

class IStreamReadStream {
   public:
    explicit IStreamReadStream(std::istream& is) : m_is(&is) {
        auto const start = is.tellg();
        is.seekg(0, std::ios::end);
        auto const end = is.tellg();
        m_totalSize = end - start;

        // move stream back to where we started
        is.seekg(start, std::ios_base::beg);
    }

    IStreamReadStream& bytes(std::span<std::byte> data, size_t count) {
        if (!ok())
            return *this;
        if (count == 0)
            return *this;
        if (!m_is->good())
            return fail(Error::StreamError);
        if (data.size() < count)
            return fail(Error::BadArg);

        m_is->read(reinterpret_cast<char*>(data.data()),
                   static_cast<std::streamsize>(count));
        if (m_is->fail())
            return fail(Error::Eof);

        m_position += count;
        return *this;
    }

    IStreamReadStream& require(bool condition, Error err) {
        if (!ok())
            return *this;
        if (!condition)
            m_status = err;
        return *this;
    }

    size_t remainingBytes() const {
        if (m_totalSize >= m_position)
            return m_totalSize - m_position;
        else
            return 0;
    }

    bool ok() const { return m_status == Error::Ok; }
    Error error() const { return m_status; }

    size_t byteSize() const { return m_position; }

   private:
    IStreamReadStream& fail(Error err) {
        m_status = err;
        return *this;
    }

    Error m_status = Error::Ok;
    size_t m_position = 0;
    size_t m_totalSize = 0;
    std::istream* m_is = nullptr;
};

}  // namespace ao::pack::byte
