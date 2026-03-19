#pragma once

#include <variant>

namespace ao::utils {
namespace detail {
template <class Variant, size_t... Idx>
bool emplaceIndex(size_t index, Variant& variant, std::index_sequence<Idx...>) {
    if (index >= sizeof...(Idx))
        return false;

    using Fn = void (*)(Variant&);
    static constexpr Fn fns[] = {
        +[](Variant& variant) { variant.emplace<Idx>(); }...};
    fns[index](variant);
    return true;
}

}  // namespace detail

template <class... VariantArgs>
bool emplaceIndex(size_t index, std::variant<VariantArgs...>& variant) {
    return detail::emplaceIndex(index, variant,
                                std::index_sequence_for<VariantArgs...>());
}
}  // namespace ao::utils
