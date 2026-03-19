#pragma once

#include <functional>
#include <tuple>

namespace ao::meta {
template <bool IsConst, class T>
using AddConst = std::conditional_t<IsConst, T const, T>;

namespace detail {
template <class Func, class Tup, size_t... Idx>
void forEach(Func&& f, Tup&& tup, std::index_sequence<Idx...>) {
    (f(std::get<Idx>(tup)), ...);
}
}  // namespace detail
template <class Func, class... Components>
void forEach(Func&& f, std::tuple<Components...>& components) {
    detail::forEach(std::forward<Func>(f), components,
                    std::index_sequence_for<Components...>());
}
template <class Func, class... Components>
void forEach(Func&& f, std::tuple<Components...> const& components) {
    detail::forEach(std::forward<Func>(f), components,
                    std::index_sequence_for<Components...>());
}
}  // namespace ao::meta
