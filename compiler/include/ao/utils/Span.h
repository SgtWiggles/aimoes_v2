#pragma once
#include <span>
#include <string>

namespace ao::utils {
template <class Value>
std::span<std::byte> makeByteSpan(Value& value) {
    return std::as_writable_bytes(std::span{&value, 1});
}
template <class Value>
std::span<std::byte const> makeByteSpan(Value const& value) {
    return std::as_bytes(std::span{&value, 1});
}

inline std::span<std::byte> makeByteSpan(std::string& str) {
    return std::as_writable_bytes(std::span{str.data(), str.size()});
}
inline std::span<std::byte const> makeByteSpan(std::string const& str) {
    return std::as_bytes(std::span{str.data(), str.size()});
}
}  // namespace ao::utils
