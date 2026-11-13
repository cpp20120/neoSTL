#pragma once

#include "traits.hpp"
#include "cpo.hpp"
#include "range_tags.hpp"
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>
#include <ranges>
#include <type_traits>

namespace neo {
// Capabilities of policy-resolved containers. These are semantic constraints,
// not tests for a particular storage or iteration tag.
template<class V>
concept growing_policy_vector = policy_vector<V> && traits::vector_traits<V>::can_grow;

template<class V>
concept non_growing_policy_vector = policy_vector<V> && !traits::vector_traits<V>::can_grow;

template<class V>
concept inline_buffer_vector = policy_vector<V> && traits::vector_traits<V>::has_inline_buffer;

template<class V>
concept strong_growth_vector = policy_vector<V> && traits::vector_traits<V>::strong_growth_guarantee;

template<class V>
concept checked_iteration_vector = policy_vector<V> && traits::vector_traits<V>::is_checked_iteration;

template<class V>
concept prefix_preserving_append_vector = checked_iteration_vector<V> &&
    traits::vector_traits<V>::preserves_append_prefix;

template<class V, std::size_t Capacity>
concept statically_reservable_vector = policy_vector<V> && requires (std::remove_cvref_t<V>& v) {
    v.template reserve_static<Capacity>();
};

template<class T, class Relocation>
concept transactionally_relocatable = detail::transactional_relocation_v<T, Relocation>;

// Range capabilities. These deliberately describe the iterator/range contract,
// independently of whether the source is a neo container, a standard container,
// or a lazy view. ``forward_only_range`` is declared in traits.hpp.
template<class R>
concept viewable_input_range = std::ranges::input_range<R> && std::ranges::viewable_range<R>;

template<class R>
concept input_only_range = std::ranges::input_range<R> && !std::ranges::forward_range<R>;

template<class R>
concept bidirectional_only_range = std::ranges::bidirectional_range<R> &&
    !std::ranges::random_access_range<R>;

template<class R>
concept random_access_only_range = std::ranges::random_access_range<R> &&
    !std::ranges::contiguous_range<R>;

template<class R>
concept common_bidirectional_range = std::ranges::bidirectional_range<R> &&
    std::ranges::common_range<R>;

template<class R>
concept sized_random_access_range = std::ranges::random_access_range<R> &&
    std::ranges::sized_range<R>;

template<class R>
concept borrowed_contiguous_range = std::ranges::contiguous_range<R> &&
    std::ranges::borrowed_range<R>;

template<class R>
concept const_iterable_range = std::ranges::input_range<const std::remove_reference_t<R>>;

template<class R>
concept mutable_forward_range = std::ranges::forward_range<R> &&
    std::permutable<std::ranges::iterator_t<R>>;

// Algorithm capabilities are separate from container-specific capabilities.
template<class R, class Compare = std::ranges::less, class Projection = std::identity>
concept mutable_sortable_range = std::ranges::random_access_range<R> &&
    std::sortable<std::ranges::iterator_t<R>, Compare, Projection>;

template<class R, class Compare = std::ranges::less, class Projection = std::identity>
concept nth_selectable_range = mutable_sortable_range<R, Compare, Projection> &&
    std::ranges::sized_range<R> && std::ranges::borrowed_range<R>;

// For iterator-returning wrappers, require borrowed_range so a temporary
// owning container cannot produce dangling iterators. Standard ranges
// algorithms may instead return std::ranges::dangling.
template<class R, class Predicate, class Projection = std::identity>
concept partitionable_range = mutable_forward_range<R> &&
    std::ranges::borrowed_range<R> &&
    std::indirect_unary_predicate<Predicate, std::projected<std::ranges::iterator_t<R>, Projection>>;

template<class R, class Predicate, class Projection = std::identity>
concept stable_partitionable_range = partitionable_range<R, Predicate, Projection> &&
    std::ranges::bidirectional_range<R>;

// Kept for source compatibility with v5-v7; now correctly supports forward.
template<class R, class Predicate, class Projection = std::identity>
concept in_place_partitionable_range = partitionable_range<R, Predicate, Projection>;

template<class R>
concept rotatable_range = mutable_forward_range<R> && std::ranges::borrowed_range<R>;

template<class R, class Predicate = std::ranges::equal_to, class Projection = std::identity>
concept unique_compactable_range = mutable_forward_range<R> &&
    std::ranges::borrowed_range<R> &&
    std::indirect_equivalence_relation<Predicate,
        std::projected<std::ranges::iterator_t<R>, Projection>>;

template<class R, class Key, class Compare = std::ranges::less, class Projection = std::identity>
concept binary_searchable_range = std::ranges::forward_range<R> &&
    std::ranges::borrowed_range<R> &&
    std::indirect_strict_weak_order<Compare, const Key*,
        std::projected<std::ranges::iterator_t<R>, Projection>>;

template<class R, class Compare = std::ranges::less, class Projection = std::identity>
concept partial_sortable_range = mutable_sortable_range<R, Compare, Projection> &&
    std::ranges::borrowed_range<R> && std::ranges::sized_range<R>;
// Container/range interoperability: these operate on public interfaces rather
// than requiring a specific neo::vector specialization.
template<class R>
concept reversible_range = std::ranges::bidirectional_range<R> &&
    std::ranges::viewable_range<R>;

template<class C, class R>
concept appendable_range = std::ranges::input_range<R> &&
    requires(C& c, R&& range) { neo::append_range(c, std::forward<R>(range)); };

template<class C>
concept reservable_container = requires(C& c, std::size_t amount) {
    neo::reserve(c, amount);
};

template<class C, class R>
concept range_constructible = std::ranges::input_range<R> &&
    requires(R&& range) { C(neo::from_range, std::forward<R>(range)); };

// Compile-time *type* compatibility is separate from runtime allocator equality:
// same stateful allocator type can still refer to different resources.
template<class A, class B>
concept allocator_equality_comparable = requires(const A& a, const B& b) {
    { a == b } -> std::convertible_to<bool>;
    { b == a } -> std::convertible_to<bool>;
};

template<class A, class B>
concept statically_interchangeable_allocators = std::same_as<A, B> &&
    std::allocator_traits<A>::is_always_equal::value;

// The allocator types can be compared for equivalence; the concept itself
// cannot promise that two particular allocator *objects* own the same resource.
template<class C1, class C2>
concept allocator_compatible = policy_vector<C1> && policy_vector<C2> &&
    std::same_as<typename traits::vector_traits<C1>::allocator_type,
                 typename traits::vector_traits<C2>::allocator_type> &&
    allocator_equality_comparable<typename traits::vector_traits<C1>::allocator_type,
                                  typename traits::vector_traits<C2>::allocator_type>;

template<class V>
concept transactionally_growable = strong_growth_vector<V> && growing_policy_vector<V>;

} // namespace neo
