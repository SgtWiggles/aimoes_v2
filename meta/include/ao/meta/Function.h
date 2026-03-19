#pragma once
#include <functional>
#include <type_traits>

#include "TypeList.h"

namespace ao::meta {
template <class F>
struct CallInfo : public CallInfo<decltype(&F::operator())> {};

template <class R, class... A>
struct CallInfo<std::function<R(A...)>> {
    using Ret = R;
    using Args = TypeList<A...>;
};
template <class R, class... A>
struct CallInfo<R(A...)> {
    using Ret = R;
    using Args = TypeList<A...>;
};

template <class R, class... A>
struct CallInfo<R (*)(A...)> {
    using Ret = R;
    using Args = TypeList<A...>;
};

// Member function
template <class R, class F, class... A>
struct CallInfo<R (F::*)(A...)> {
    using Ret = R;
    using Args = TypeList<A...>;
};
template <class R, class F, class... A>
struct CallInfo<R (F::*)(A...) const> {
    using Ret = R;
    using Args = TypeList<A...>;
};

}  // namespace ao::meta
