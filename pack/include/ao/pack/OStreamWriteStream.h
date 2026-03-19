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

}  // namespace ao::pack::byte
