#include <ao/pack/ByteStream.h>

#include <algorithm>
#include <limits>

namespace ao::pack::byte {
ReadStream& ReadStream::bytes(std::span<std::byte> out, size_t count) {
    auto success = peek(out, count);
    if (success)
        m_position += count;
    else
        fail(ao::pack::Error::Eof);
    return *this;
}

bool ReadStream::peek(std::span<std::byte> out, size_t count) {
    if (!ok())
        return false;
    if (count == 0)
        return true;
    if (out.size() < count)
        return false;
    if (remainingBytes() < count)
        return false;
    auto src = m_data.subspan(m_position, count);
    std::copy(src.begin(), src.end(), out.begin());
    return true;
}
ReadStream& ReadStream::require(bool condition, Error err) {
    if (!ok())
        return *this;
    if (!condition)
        m_status = err;
    return *this;
}

WriteStream& WriteStream::bytes(std::span<std::byte const> out, size_t count) {
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
