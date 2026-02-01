#pragma once

namespace ao {
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
}  // namespace ao
