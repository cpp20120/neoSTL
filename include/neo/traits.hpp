#pragma once

#include "policies.hpp"
#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace neo::traits {

template<class S>
inline constexpr bool is_growing_storage_v = storage_traits<S>::can_grow;
template<class S>
inline constexpr bool has_inline_storage_v = storage_traits<S>::has_inline_buffer;
template<class S>
inline constexpr bool is_borrowed_storage_v = !storage_traits<S>::owns_buffer;

template<class G> struct growth_traits {
    static constexpr bool enabled = !std::same_as<G, growth::none>;
    static constexpr bool is_nothrow = !enabled ||
        std::is_nothrow_invocable_r_v<std::size_t, G, std::size_t, std::size_t>;
};
template<class B> struct bounds_traits;
template<> struct bounds_traits<bounds::checked> {
    static constexpr bool is_checked = true;
    static constexpr bool indexed_access_is_nothrow = false;
};
template<> struct bounds_traits<bounds::unchecked> {
    static constexpr bool is_checked = false;
    static constexpr bool indexed_access_is_nothrow = true;
};
template<class I> struct iteration_traits;
template<> struct iteration_traits<iteration::raw> {
    static constexpr bool preserves_prefix_on_nonrealloc_append = false;
    static constexpr bool is_checked = false;
    static constexpr bool is_contiguous = true;
    static constexpr bool invalidation_is_detected = false;
};
template<> struct iteration_traits<iteration::checked> {
    static constexpr bool preserves_prefix_on_nonrealloc_append = false;
    static constexpr bool is_checked = true;
    static constexpr bool is_contiguous = true;
    static constexpr bool invalidation_is_detected = true;
};

template<class E> struct exception_safety_traits;
template<> struct exception_safety_traits<exception_safety::basic> {
    static constexpr bool strong_growth = false;
};
template<> struct exception_safety_traits<exception_safety::strong_growth> {
    static constexpr bool strong_growth = true;
};

template<> struct iteration_traits<iteration::checked_prefix> {
    static constexpr bool preserves_prefix_on_nonrealloc_append = true;
    static constexpr bool is_checked = true;
    static constexpr bool is_contiguous = true;
    static constexpr bool invalidation_is_detected = true;
};

template<class T, class Relocation> struct relocation_traits;
template<class T> struct relocation_traits<T, relocation::always_move> {
    static constexpr bool uses_move = true;
    static constexpr bool is_nothrow = std::is_nothrow_constructible_v<T, T&&>;
};
template<class T> struct relocation_traits<T, relocation::copy> {
    static constexpr bool uses_move = false;
    static constexpr bool is_nothrow = std::is_nothrow_constructible_v<T, const T&>;
};
template<class T> struct relocation_traits<T, relocation::move_if_noexcept> {
    static constexpr bool uses_move = std::is_nothrow_move_constructible_v<T> ||
                                    !std::is_copy_constructible_v<T>;
    static constexpr bool is_nothrow = std::is_nothrow_constructible_v<T,
        decltype(std::move_if_noexcept(std::declval<T&>()))>;
};

template<class T, class Allocation>
struct allocation_traits {
    using allocator_type = typename detail::allocator_for<T, Allocation>::type;
    using std_traits = std::allocator_traits<allocator_type>;
    static constexpr bool is_pmr = std::same_as<Allocation, allocation::pmr>;
    static constexpr bool is_always_equal = std_traits::is_always_equal::value;
    static constexpr bool propagate_on_move_assignment =
        std_traits::propagate_on_container_move_assignment::value;
};

template<class Config> struct config_traits {
    using storage_policy = typename Config::storage_type;
    using growth_policy = typename Config::growth_type;
    using bounds_policy = typename Config::bounds_type;
    using relocation_policy = typename Config::relocation_type;
    using allocation_policy = typename Config::allocation_type;
    using iteration_policy = typename Config::iteration_type;
    using exception_safety_policy = typename Config::exception_safety_type;
    using allocator_type = typename Config::allocator_type;
    static constexpr bool can_grow = storage_traits<storage_policy>::can_grow;
    static constexpr bool has_inline_buffer = storage_traits<storage_policy>::has_inline_buffer;
    static constexpr bool owns_buffer = storage_traits<storage_policy>::owns_buffer;
    static constexpr bool has_compile_time_capacity =
        storage_traits<storage_policy>::has_compile_time_capacity;
    static constexpr std::size_t inline_capacity = storage_traits<storage_policy>::inline_capacity;
    static constexpr bool is_checked_iteration = iteration_traits<iteration_policy>::is_checked;
    static constexpr bool preserves_append_prefix =
        iteration_traits<iteration_policy>::preserves_prefix_on_nonrealloc_append;
    static constexpr bool strong_growth_guarantee =
        exception_safety_traits<exception_safety_policy>::strong_growth;
    static constexpr bool is_checked_bounds = bounds_traits<bounds_policy>::is_checked;
    static constexpr bool relocation_is_nothrow =
        relocation_traits<typename Config::value_type, relocation_policy>::is_nothrow;
};

// A user may inspect all resolved capabilities of neo::vector without
// depending on its concrete policy pack ordering.
template<class V, class = void> struct vector_traits;
template<class V>
struct vector_traits<V, std::void_t<typename std::remove_cvref_t<V>::policy_config>> :
    config_traits<typename std::remove_cvref_t<V>::policy_config> {};

template<std::ranges::range R> struct range_traits {
    static constexpr bool input = std::ranges::input_range<R>;
    static constexpr bool forward = std::ranges::forward_range<R>;
    static constexpr bool input_only = input && !forward;
    static constexpr bool forward_only = forward && !std::ranges::bidirectional_range<R>;
    static constexpr bool bidirectional = std::ranges::bidirectional_range<R>;
    static constexpr bool bidirectional_only = bidirectional && !std::ranges::random_access_range<R>;
    static constexpr bool random_access = std::ranges::random_access_range<R>;
    static constexpr bool contiguous = std::ranges::contiguous_range<R>;
    static constexpr bool random_access_only = random_access && !contiguous;
    static constexpr bool mutable_forward = forward && std::permutable<std::ranges::iterator_t<R>>;
    static constexpr bool viewable = std::ranges::viewable_range<R>;
    static constexpr bool is_view = std::ranges::view<std::remove_cvref_t<R>>;
    static constexpr bool sized = std::ranges::sized_range<R>;
    static constexpr bool common = std::ranges::common_range<R>;
    static constexpr bool borrowed = std::ranges::borrowed_range<R>;
};

} // namespace neo::traits

namespace neo {
template<class S> concept growing_storage = traits::is_growing_storage_v<S>;
template<class S> concept non_growing_storage = is_non_growing_storage_v<S>;
template<class V> concept policy_vector = requires { typename traits::vector_traits<V>::storage_policy; };
template<class R> concept forward_only_range = std::ranges::range<R> && traits::range_traits<R>::forward_only;
} // namespace neo
