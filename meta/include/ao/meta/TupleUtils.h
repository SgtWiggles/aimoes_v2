#pragma once

#include <optional>
#include <tuple>
#include <utility>

namespace ao::meta {
namespace detail {
template <class Type, class Arg, size_t Idx>
consteval bool getTupleIndexSingle(std::optional<size_t>& output) {
    if constexpr (std::is_same_v<Type, Arg>) {
        output = Idx;
        return true;
    }
    return false;
}
template <class Type, class... Args, size_t... Idx>
consteval std::optional<size_t> getTupleIndex(std::index_sequence<Idx...>) {
    std::optional<size_t> ret;
    (false || ... || detail::getTupleIndexSingle<Type, Args, Idx>(ret));
    return ret;
}
}  // namespace detail

template <class Type, class Tuple>
struct GetTupleIndex;
template <class Type, class... Args>
struct GetTupleIndex<Type, std::tuple<Args...>> {
    static constexpr std::optional<size_t> value =
        detail::getTupleIndex<Type, Args...>(
            std::make_index_sequence<sizeof...(Args)>());
};

template <class Type, class Tuple>
constexpr bool hasType = GetTupleIndex<Type, Tuple>::value.has_value();

template <class Type, class Tuple>
Type* getIf(Tuple& tup) {
    static constexpr auto Idx = GetTupleIndex<Type, Tuple>::value;
    if constexpr (Idx.has_value()) {
        return &std::get<Idx.value()>(tup);
    } else {
        return nullptr;
    }
}
template <class Type, class Tuple>
Type const* getIf(Tuple const& tup) {
    static constexpr auto Idx = GetTupleIndex<Type, Tuple>::value;
    if constexpr (Idx.has_value()) {
        return &std::get<Idx.value()>(tup);
    } else {
        return nullptr;
    }
}
}  // namespace ao::meta