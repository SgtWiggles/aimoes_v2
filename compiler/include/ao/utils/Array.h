#pragma once

#include <array>

template <class T, class... Args>
constexpr std::array<T, sizeof...(Args)> makeArray(Args&&... args) {
    return {std::forward<Args>(args)...};
}
