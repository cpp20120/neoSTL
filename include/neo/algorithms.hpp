#pragma once
#include "concepts.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace neo {
template<class R>
struct static_extent : std::integral_constant<std::size_t, std::dynamic_extent> {};
template<class T, std::size_t N>
struct static_extent<std::span<T, N>> : std::integral_constant<std::size_t, N> {};
template<class R>
inline constexpr std::size_t static_extent_v = static_extent<std::remove_cvref_t<R>>::value;

template<class Projection = std::identity, class Compare = std::ranges::less>
struct ordering {
    [[no_unique_address]] Projection projection{};
    [[no_unique_address]] Compare compare{};
};
namespace traits {
template<class O> struct ordering_traits;
template<class P, class C> struct ordering_traits<ordering<P, C>> {
    using projection_type = P;
    using compare_type = C;
    static constexpr bool is_identity_projection = std::same_as<P, std::identity>;
};
}
template<class Projection>
constexpr auto by(Projection p) noexcept(std::is_nothrow_move_constructible_v<Projection>) {
    return ordering<Projection>{std::move(p), {}};
}
template<class Projection, class Compare>
constexpr auto by(Projection p, Compare c)
    noexcept(std::is_nothrow_move_constructible_v<Projection> &&
             std::is_nothrow_move_constructible_v<Compare>) {
    return ordering<Projection, Compare>{std::move(p), std::move(c)};
}

// ADL customization is opt-in. A custom sort must return void, and custom
// selection/partition must return the iterator type of the borrowed range.
// The custom implementation is responsible for the same semantic postconditions
// as the standard algorithm. Constraints cannot prove those postconditions.
template<sort_mode Mode, class R, class P = std::identity, class C = std::ranges::less>
concept custom_sortable_range = std::ranges::range<R> &&
    detail::void_tag_invocable<sort_tag<Mode>, R, ordering<P, C>>;

template<sort_mode Mode, class R, class P = std::identity, class C = std::ranges::less>
concept sort_dispatchable = custom_sortable_range<Mode, R, P, C> ||
    mutable_sortable_range<R, C, P>;

template<class R, class P = std::identity, class C = std::ranges::less>
concept custom_nth_selectable_range = std::ranges::random_access_range<R> &&
    std::ranges::sized_range<R> && std::ranges::borrowed_range<R> &&
    detail::tag_invocable<nth_element_tag, R, std::size_t, ordering<P, C>> &&
    std::same_as<detail::tag_invoke_result_t<nth_element_tag, R, std::size_t, ordering<P, C>>,
                 std::ranges::iterator_t<R>>;

template<class R, class P = std::identity, class C = std::ranges::less>
concept nth_dispatchable = custom_nth_selectable_range<R, P, C> ||
    nth_selectable_range<R, C, P>;

template<bool Stable, class R, class Pred, class Proj = std::identity>
concept custom_partitionable_range = std::ranges::forward_range<R> &&
    std::ranges::borrowed_range<R> && (!Stable || std::ranges::bidirectional_range<R>) &&
    detail::tag_invocable<partition_tag<Stable>, R, Pred, Proj> &&
    std::same_as<detail::tag_invoke_result_t<partition_tag<Stable>, R, Pred, Proj>,
                 std::ranges::iterator_t<R>>;

template<bool Stable, class R, class Pred, class Proj = std::identity>
concept partition_dispatchable = custom_partitionable_range<Stable, R, Pred, Proj> ||
    (Stable ? stable_partitionable_range<R, Pred, Proj> :
              partitionable_range<R, Pred, Proj>);

namespace detail {
// The two constrained overloads are deliberately disjoint. A matching
// user-provided tag_invoke wins even when the generic algorithm is viable.
template<sort_mode Mode, class R, class P, class C>
    requires custom_sortable_range<Mode, R, P, C>
constexpr void sort_dispatch(R&& r, ordering<P, C> opts)
    noexcept(nothrow_tag_invocable<sort_tag<Mode>, R, ordering<P, C>>) {
    tag_invoke(sort_tag<Mode>{}, std::forward<R>(r), std::move(opts));
}

template<sort_mode Mode, class R, class P, class C>
    requires (!custom_sortable_range<Mode, R, P, C> &&
              mutable_sortable_range<R, C, P>)
constexpr void sort_dispatch(R&& r, ordering<P, C> opts) {
    if constexpr (Mode == sort_mode::stable)
        std::ranges::stable_sort(r, opts.compare, opts.projection);
    else
        std::ranges::sort(r, opts.compare, opts.projection);
}

template<class R, class P, class C>
    requires custom_nth_selectable_range<R, P, C>
constexpr auto nth_dispatch(R&& r, std::size_t k, ordering<P, C> opts)
    noexcept(nothrow_tag_invocable<nth_element_tag, R, std::size_t, ordering<P, C>>) {
    return tag_invoke(nth_element_tag{}, std::forward<R>(r), k, std::move(opts));
}

template<class R, class P, class C>
    requires (!custom_nth_selectable_range<R, P, C> &&
              nth_selectable_range<R, C, P>)
constexpr auto nth_dispatch(R&& r, std::size_t k, ordering<P, C> opts) {
    auto nth = std::ranges::begin(r) + static_cast<std::ranges::range_difference_t<R>>(k);
    std::ranges::nth_element(r, nth, opts.compare, opts.projection);
    return nth;
}

template<bool Stable, class R, class Pred, class Proj>
    requires custom_partitionable_range<Stable, R, Pred, Proj>
constexpr auto partition_dispatch(R&& r, Pred pred, Proj proj)
    noexcept(nothrow_tag_invocable<partition_tag<Stable>, R, Pred, Proj>) {
    return tag_invoke(partition_tag<Stable>{}, std::forward<R>(r),
                      std::move(pred), std::move(proj));
}

template<bool Stable, class R, class Pred, class Proj>
    requires (!custom_partitionable_range<Stable, R, Pred, Proj> &&
              (Stable ? stable_partitionable_range<R, Pred, Proj> :
                        partitionable_range<R, Pred, Proj>))
constexpr auto partition_dispatch(R&& r, Pred pred, Proj proj) {
    if constexpr (Stable)
        return std::ranges::stable_partition(r, std::move(pred), std::move(proj)).begin();
    else
        return std::ranges::partition(r, std::move(pred), std::move(proj)).begin();
}
} // namespace detail

// Function-template façade preserves sort<Mode>(r), unlike a function object.
template<sort_mode Mode = sort_mode::fast,
         class R, class P = std::identity, class C = std::ranges::less>
    requires sort_dispatchable<Mode, R, P, C>
constexpr void sort(R&& range, ordering<P, C> options = {})
    noexcept(noexcept(detail::sort_dispatch<Mode>(std::forward<R>(range), std::move(options)))) {
    detail::sort_dispatch<Mode>(std::forward<R>(range), std::move(options));
}

template<class R, class P = std::identity, class C = std::ranges::less>
    requires sort_dispatchable<sort_mode::stable, R, P, C>
constexpr void sort(R&& range, stable_t, ordering<P, C> options = {})
    noexcept(noexcept(detail::sort_dispatch<sort_mode::stable>(
        std::forward<R>(range), std::move(options)))) {
    detail::sort_dispatch<sort_mode::stable>(std::forward<R>(range), std::move(options));
}

// Keep the dedicated span overload in the public API while routing through
// exactly the same CPO/algorithm dispatch.
template<class T, std::size_t N, class P = std::identity, class C = std::ranges::less>
    requires sort_dispatchable<sort_mode::fast, std::span<T, N>, P, C>
constexpr void sort(std::span<T, N> range, ordering<P, C> options = {})
    noexcept(noexcept(detail::sort_dispatch<sort_mode::fast>(range, std::move(options)))) {
    if constexpr (custom_sortable_range<sort_mode::fast, std::span<T, N>, P, C> ||
                  N == std::dynamic_extent || N > 1)
        detail::sort_dispatch<sort_mode::fast>(range, std::move(options));
}

template<class R, class P = std::identity, class C = std::ranges::less>
    requires nth_dispatchable<R, P, C>
constexpr auto nth_element(R&& range, std::size_t k, ordering<P, C> options = {}) {
    if (k >= static_cast<std::size_t>(std::ranges::size(range)))
        throw std::out_of_range("neo::nth_element index");
    return detail::nth_dispatch(std::forward<R>(range), k, std::move(options));
}

template<std::size_t K, class R, class P = std::identity, class C = std::ranges::less>
    requires nth_dispatchable<R, P, C>
constexpr auto nth_element(R&& range, ordering<P, C> options = {}) {
    return nth_element(std::forward<R>(range), K, std::move(options));
}

template<class R, class Predicate, class Projection = std::identity>
    requires partition_dispatchable<false, R, Predicate, Projection>
constexpr auto partition(R&& range, Predicate pred, Projection proj = {})
    noexcept(noexcept(detail::partition_dispatch<false>(
        std::forward<R>(range), std::move(pred), std::move(proj)))) {
    return detail::partition_dispatch<false>(std::forward<R>(range),
                                             std::move(pred), std::move(proj));
}

template<class R, class Predicate, class Projection = std::identity>
    requires partition_dispatchable<true, R, Predicate, Projection>
constexpr auto partition(R&& range, stable_t, Predicate pred, Projection proj = {})
    noexcept(noexcept(detail::partition_dispatch<true>(
        std::forward<R>(range), std::move(pred), std::move(proj)))) {
    return detail::partition_dispatch<true>(std::forward<R>(range),
                                            std::move(pred), std::move(proj));
}

// Other v8 algorithms retain their existing range/borrowed contracts.
template<class R>
    requires rotatable_range<R>
auto rotate(R&& range, std::ranges::iterator_t<R> middle) {
    return std::ranges::rotate(range, std::move(middle)).begin();
}

template<class R, class Predicate = std::ranges::equal_to, class Projection = std::identity>
    requires unique_compactable_range<R, Predicate, Projection>
auto unique(R&& range, Predicate eq = {}, Projection proj = {}) {
    return std::ranges::unique(range, std::move(eq), std::move(proj));
}

template<class R, class Key, class P = std::identity, class C = std::ranges::less>
    requires binary_searchable_range<R, Key, C, P>
auto lower_bound(R&& range, const Key& key, ordering<P, C> options = {}) {
    return std::ranges::lower_bound(range, key, options.compare, options.projection);
}

template<class R, class Key, class P = std::identity, class C = std::ranges::less>
    requires binary_searchable_range<R, Key, C, P>
auto upper_bound(R&& range, const Key& key, ordering<P, C> options = {}) {
    return std::ranges::upper_bound(range, key, options.compare, options.projection);
}
} // namespace neo
