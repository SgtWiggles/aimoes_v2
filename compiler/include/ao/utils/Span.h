#pragma once
#include <span>

namespace ao::utils {
template <class Value>
std::span<std::byte> makeByteSpan(Value& value) {
    return std::as_writable_bytes(std::span{&value, 1});
}
template <class Value>
std::span<std::byte const> makeByteSpan(Value const& value) {
    return std::as_bytes(std::span{&value, 1});
}
}  // namespace ao::utils
