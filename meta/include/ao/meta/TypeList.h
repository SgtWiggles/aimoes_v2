#pragma once
#include <utility>

namespace ao::meta {
template <class... Args>
struct TypeList {};

template <class... Args>
auto indexSequenceFor(TypeList<Args...>) {
    return std::index_sequence_for<Args...>();
}
}  // namespace ao::meta
