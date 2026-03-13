#pragma once

#include <array>

namespace ao {
template <class T, class... Args>
constexpr std::array<T, sizeof...(Args)> makeArray(Args&&... args) {
    return {std::forward<Args>(args)...};
}
template<class T, class F>
void enumerate(T const& data, F f) {
    size_t i = 0; 
    for (auto const& v : data) {
        f(i, v);
        ++i;
    }
}
}

