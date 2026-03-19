#pragma once
#include <memory>
#include <type_traits>

namespace ao::meta {
template <class T>
struct IsShared : std::false_type {};
template <class T>
struct IsShared<std::shared_ptr<T>> : std::true_type {};
template <class T>
constexpr inline bool IsShared_v = IsShared<T>::value;

template <class V>
struct RemoveShared {
    using T = V;
};
template <class Ptr>
struct RemoveShared<std::shared_ptr<Ptr>> {
    using T = Ptr;
};
template <class T>
using RemoveShared_t = typename RemoveShared<T>::T;
}  // namespace ao::meta
