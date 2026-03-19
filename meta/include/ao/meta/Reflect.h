#pragma once

#include <string_view>
#include <tuple>
#include <type_traits>

namespace ao::meta {
#define AO_PP_UNPAREN(...) AO_PP_ESC(ISH_AO_PP __VA_ARGS__)
#define ISH_AO_PP(...) ISH_AO_PP __VA_ARGS__
#define AO_PP_ESC(...) AO_PP_ESC2(__VA_ARGS__)
#define AO_PP_ESC2(...) VAN##__VA_ARGS__
#define VANISH_AO_PP

namespace detail {
struct Empty;

template <template <class, size_t> class Info, class Tag, size_t Idx>
struct MemberIndexImpl {
    static constexpr size_t value = 0;
};
template <template <class, size_t> class Info, class Tag, size_t Idx>
    requires requires { Info<Tag, Idx>::name; }
struct MemberIndexImpl<Info, Tag, Idx> {
    static constexpr size_t value =
        1 + MemberIndexImpl<Info, Tag, Idx + 1>::value;
};

template <template <class, size_t> class MemberInfo, class Tag>
struct MemberIndex {
    static constexpr size_t value = MemberIndexImpl<MemberInfo, Tag, 0>::value;
};

}  // namespace detail

template <class T>
struct MemberCount {
    static constexpr size_t value =
        detail::MemberIndex<T::zz_ao_member_info, detail::Empty>::value;
};
template <class T>
static constexpr size_t MemberCount_v = MemberCount<T>::value;

template <class T>
struct IsReflectable {
    template <class T, class = void*>
    struct Impl : std::false_type {};
    template <class T>
    struct Impl<T,
                decltype(std::declval<typename T::zz_ao_reflect_tag>(),
                         (void*)nullptr)> : std::true_type {};
    static constexpr auto value = Impl<T>::value;
};
template <class T>
concept Reflectable = IsReflectable<T>::value;

#define AO_IMPL_MEMBER(TYPE, NAME, ATTRIBUTES)                         \
    template <class, size_t>                                           \
    struct zz_ao_member_info;                                          \
    static constexpr auto zz_ao_member_index_##NAME =                  \
        ::ao::meta::detail::MemberIndex<                               \
            zz_ao_member_info, struct zz_ao_member_tag_##NAME>::value; \
    template <class Tag>                                               \
    struct zz_ao_member_info<Tag, zz_ao_member_index_##NAME> {         \
        static constexpr std::string_view const name = #NAME;          \
        static constexpr auto attributes =                             \
            std::tuple{AO_PP_UNPAREN(ATTRIBUTES)};                     \
        using Type = AO_PP_UNPAREN(TYPE);                              \
        template <class U>                                             \
        static constexpr auto dataOffset = &U::NAME;                   \
    };                                                                 \
    struct zz_ao_reflect_tag;                                          \
    template <class>                                                   \
    friend struct ::ao::meta::MemberCount;                             \
    template <class, size_t>                                           \
    friend struct ::ao::meta::MemberInfo;                              \
    template <class T>                                                 \
    friend struct ::ao::meta::IsReflectable;                           \
    AO_PP_UNPAREN(TYPE) NAME

#define AO_EXPAND(...) __VA_ARGS__
#define AO_GET_MACRO3(_1, _2, _3, NAME, ...) NAME
#define AO_MEMBER2(TYPE, NAME) AO_IMPL_MEMBER(TYPE, NAME, ())
#define AO_MEMBER3(TYPE, NAME, ATTRIBUTES) \
    AO_IMPL_MEMBER(TYPE, NAME, ATTRIBUTES)
#define AO_MEMBER(...) \
    AO_EXPAND(AO_GET_MACRO3(__VA_ARGS__, AO_MEMBER3, AO_MEMBER2)(__VA_ARGS__))

template <class T, size_t Idx>
struct MemberInfo {
    using Type = typename T::template zz_ao_member_info<detail::Empty, Idx>;
};
template <class T, size_t Idx>
using MemberInfo_t = typename MemberInfo<T, Idx>::Type;

namespace detail {
template <size_t Idx, class Func, Reflectable T>
constexpr void visitSingle(Func f, T& value) {
    using Member = MemberInfo_t<T, Idx>;
    f(value.*Member::template dataOffset<std::decay_t<T>>, Member{});
}
template <class Func, Reflectable T, size_t... Idx>
constexpr void visit(Func f, T& value, ::std::index_sequence<Idx...>) {
    (visitSingle<Idx>(f, value), ...);
}
}  // namespace detail

template <class Func, Reflectable T>
constexpr void visit(Func f, T& value) {
    detail::visit(std::move(f), value,
                  std::make_index_sequence<MemberCount_v<T>>());
}

namespace detail {
template <size_t Idx, Reflectable T, class Func>
constexpr void visitMetaSingle(Func f) {
    using Member = MemberInfo_t<T, Idx>;
    f(Member{});
}
template <Reflectable T, class Func, size_t... Idx>
constexpr void visitMeta(Func f, ::std::index_sequence<Idx...>) {
    (visitMetaSingle<Idx, T>(f), ...);
}
}  // namespace detail
template <Reflectable T, class Func>
constexpr void visitMeta(Func f) {
    detail::visitMeta<T>(f, std::make_index_sequence<MemberCount_v<T>>());
}
}  // namespace ao::meta
