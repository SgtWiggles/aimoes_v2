#include <ao/pack/ByteStream.h>

#include <limits>

namespace ao::pack::byte {
ReadStream& ReadStream::bytes(std::span<std::byte const>& out, size_t count) {
    if (!ok())
        return *this;
    if (remainingBytes() < count)
        return fail(Error::Eof);

    out = m_data.subspan(m_position, count);
    m_position += count;
    return *this;
}
ReadStream& ReadStream::require(bool condition, Error err) {
    if (!ok())
        return *this;
    if (!condition)
        m_status = err;
    return *this;
}

WriteStream& WriteStream::bytes(std::span<std::byte> out, size_t count) {
    if (!ok())
        return *this;
    if (remainingBytes() < count)
        return fail(Error::Overflow);

    auto tmp = out.subspan(0, count);
    std::copy(tmp.begin(), tmp.end(), m_data.begin() + m_position);
    m_position += count;

    return *this;
}
WriteStream& WriteStream::require(bool condition, Error err) {
    if (!ok())
        return *this;
    if (!condition)
        m_status = err;
    return *this;
}
}  // namespace ao::pack::byte
