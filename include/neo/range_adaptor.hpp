#pragma once

#include <concepts>
#include <ranges>
#include <type_traits>
#include <utility>

namespace neo::views {

// A shared base for neo adaptors. On conforming C++23 libraries use the
// standard adaptor-closure machinery, which supports both neo | std::views
// and std::views | neo composition without duplicating overload sets.
#if defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 202202L
inline constexpr bool has_standard_adaptor_closure = true;
template<class Derived>
using adaptor_closure = std::ranges::range_adaptor_closure<Derived>;
#else
// Compatibility mode for older C++23 standard libraries. This preserves
// range | neo and neo | neo; mixed adaptor | adaptor composition requires
// std::ranges::range_adaptor_closure (P2387R3).
inline constexpr bool has_standard_adaptor_closure = false;

struct closure_marker {};

template<class L, class R>
struct composed_closure;

template<class Derived>
struct adaptor_closure : closure_marker {
    template<std::ranges::viewable_range R>
        requires requires(Derived const& adaptor, R&& r) {
            adaptor(std::forward<R>(r));
        }
    friend constexpr auto operator|(R&& r, Derived const& adaptor)
        noexcept(noexcept(adaptor(std::forward<R>(r)))) {
        return adaptor(std::forward<R>(r));
    }

    template<class Right>
        requires std::derived_from<Right, closure_marker>
    friend constexpr auto operator|(Derived const& lhs, Right const& rhs) {
        return composed_closure<Derived, Right>{lhs, rhs};
    }
};

template<class L, class R>
struct composed_closure : adaptor_closure<composed_closure<L, R>> {
    [[no_unique_address]] L lhs;
    [[no_unique_address]] R rhs;
    constexpr composed_closure(L l, R r) : lhs(std::move(l)), rhs(std::move(r)) {}

    template<std::ranges::viewable_range Range>
        requires requires(const L& l, const R& r, Range&& range) {
            r(l(std::forward<Range>(range)));
        }
    constexpr auto operator()(Range&& range) const {
        return rhs(lhs(std::forward<Range>(range)));
    }
};
#endif

template<class C, class R>
concept adaptor_for = std::ranges::viewable_range<R> &&
    requires(C& adaptor, R&& range) {
        { adaptor(std::forward<R>(range)) } -> std::ranges::view;
    };

} // namespace neo::views
