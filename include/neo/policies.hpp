#pragma once

#include <concepts>
#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <span>
#include <type_traits>

namespace neo {
// Policy bundles are types, not objects: no runtime cost or change to the
// existing vector<T, Policies...> interface. Bundles may be nested.
template<class... Policies>
struct policy_pack {};

// Specialize this for a named user-defined preset tag:
// template<> struct neo::preset_traits<my_preset> {
//     using policies = neo::policy_pack<neo::bounds::checked, ...>;
// };
template<class Tag>
struct preset_traits;

namespace storage {
struct heap {};
template<std::size_t N> struct small { static constexpr std::size_t inline_capacity = N; };
template<std::size_t N> struct fixed { static constexpr std::size_t inline_capacity = N; };
// The vector owns constructed T objects, but borrows the caller's raw byte buffer.
struct external {};
}

namespace allocation {
struct standard {};
struct pmr {};
template<class Allocator> struct with { using allocator_type = Allocator; };
}

namespace iteration {
struct raw {};
// Conservative: every successful size/capacity mutation invalidates all.
struct checked {};
// With spare capacity, append preserves existing element iterators but
// invalidates iterators that were end(); all other mutations invalidate all.
struct checked_prefix {};
}

namespace bounds {
struct unchecked {};
struct checked {};
}

namespace growth {
struct none {};
// An arbitrary compile-time callable (including a captureless lambda) is a
// growth policy. NTTP is deliberately kept out of the happy-path API.
template<auto Function>
struct custom {
    static constexpr std::size_t operator()(std::size_t current, std::size_t required)
        noexcept(noexcept(Function(current, required))) {
        return Function(current, required);
    }
};

struct exact {
    static constexpr std::size_t operator()(std::size_t, std::size_t required) noexcept {
        return required;
    }
};
template<std::size_t Num = 3, std::size_t Den = 2>
struct geometric {
    static_assert(Den != 0 && Num > Den, "growth factor must exceed one");
    static constexpr std::size_t operator()(std::size_t cap, std::size_t minimum) noexcept {
        cap = cap ? cap : 1;
        while (cap < minimum) {
            const auto delta = Num - Den;
            const auto q = cap / Den;
            const auto r = cap % Den;
            constexpr auto max = std::numeric_limits<std::size_t>::max();
            if (q > max / delta) return minimum;
            auto increment = q * delta;
            if (r > max / delta) return minimum;
            auto remainder = (r * delta) / Den;
            if (increment > max - remainder) return minimum;
            increment += remainder;
            if (increment == 0) increment = 1;
            if (increment > max - cap) return minimum;
            cap += increment;
        }
        return cap;
    }
};
}

namespace relocation {
struct move_if_noexcept {};
struct always_move {};
struct copy {};
}

namespace exception_safety {
// Basic validity on failure; existing elements may be moved-from when
// a throwing relocation fails.
struct basic {};
// Strong guarantee specifically for reserve() and a *single* emplace_back()
// which causes capacity growth. Other bulk/mutating operations are not
// promised to be transactional.
struct strong_growth {};
}

namespace traits {
// Public, independently usable policy traits. These express capabilities,
// not just which implementation tag was selected.
template<class Storage> struct storage_traits;

template<> struct storage_traits<storage::heap> {
    static constexpr bool can_grow = true;
    static constexpr bool has_inline_buffer = false;
    static constexpr bool owns_buffer = true;
    static constexpr bool can_allocate = true;
    static constexpr bool has_compile_time_capacity = false;
    static constexpr std::size_t inline_capacity = 0;
};
template<std::size_t N> struct storage_traits<storage::small<N>> {
    static constexpr bool can_grow = true;
    static constexpr bool has_inline_buffer = true;
    static constexpr bool owns_buffer = true;
    static constexpr bool can_allocate = true;
    static constexpr bool has_compile_time_capacity = false; // N is inline threshold, not a hard cap
    static constexpr std::size_t inline_capacity = N;
};
template<std::size_t N> struct storage_traits<storage::fixed<N>> {
    static constexpr bool can_grow = false;
    static constexpr bool has_inline_buffer = true;
    static constexpr bool owns_buffer = true;
    static constexpr bool can_allocate = false;
    static constexpr bool has_compile_time_capacity = true;
    static constexpr std::size_t inline_capacity = N;
};
template<> struct storage_traits<storage::external> {
    static constexpr bool can_grow = false;
    static constexpr bool has_inline_buffer = false;
    static constexpr bool owns_buffer = false; // constructed objects are owned; backing bytes are borrowed
    static constexpr bool can_allocate = false;
    static constexpr bool has_compile_time_capacity = false; // runtime span size
    static constexpr std::size_t inline_capacity = 0;
};


// Canonical capability: a storage can be inline and still support growth.
template<class Storage>
inline constexpr bool is_non_growing_storage_v = !storage_traits<Storage>::can_grow;
} // namespace traits

// Convenience alias for library clients and requires expressions.
template<class Storage> inline constexpr bool is_non_growing_storage_v =
    traits::is_non_growing_storage_v<Storage>;

namespace detail {
enum class policy_category { unknown, storage, growth, bounds, relocation, allocation, iteration, exception_safety };
template<class P> struct category : std::integral_constant<policy_category, policy_category::unknown> {};
template<> struct category<storage::heap> : std::integral_constant<policy_category, policy_category::storage> {};
template<std::size_t N> struct category<storage::small<N>> : std::integral_constant<policy_category, policy_category::storage> {};
template<std::size_t N> struct category<storage::fixed<N>> : std::integral_constant<policy_category, policy_category::storage> {};
template<> struct category<storage::external> : std::integral_constant<policy_category, policy_category::storage> {};
template<> struct category<growth::none> : std::integral_constant<policy_category, policy_category::growth> {};
template<> struct category<growth::exact> : std::integral_constant<policy_category, policy_category::growth> {};
template<auto F> struct category<growth::custom<F>> : std::integral_constant<policy_category, policy_category::growth> {};
template<std::size_t N, std::size_t D> struct category<growth::geometric<N,D>> : std::integral_constant<policy_category, policy_category::growth> {};
template<> struct category<bounds::unchecked> : std::integral_constant<policy_category, policy_category::bounds> {};
template<> struct category<bounds::checked> : std::integral_constant<policy_category, policy_category::bounds> {};
template<> struct category<relocation::move_if_noexcept> : std::integral_constant<policy_category, policy_category::relocation> {};
template<> struct category<relocation::always_move> : std::integral_constant<policy_category, policy_category::relocation> {};
template<> struct category<relocation::copy> : std::integral_constant<policy_category, policy_category::relocation> {};

template<> struct category<allocation::standard> : std::integral_constant<policy_category, policy_category::allocation> {};
template<> struct category<allocation::pmr> : std::integral_constant<policy_category, policy_category::allocation> {};
template<class A> struct category<allocation::with<A>> : std::integral_constant<policy_category, policy_category::allocation> {};
template<> struct category<iteration::raw> : std::integral_constant<policy_category, policy_category::iteration> {};
template<> struct category<iteration::checked> : std::integral_constant<policy_category, policy_category::iteration> {};
template<> struct category<iteration::checked_prefix> : std::integral_constant<policy_category, policy_category::iteration> {};
template<> struct category<exception_safety::basic> : std::integral_constant<policy_category, policy_category::exception_safety> {};
template<> struct category<exception_safety::strong_growth> : std::integral_constant<policy_category, policy_category::exception_safety> {};

template<class P>
inline constexpr auto category_v = category<P>::value;

// Expand bundles recursively, including user-defined named preset tags. A
// recognized concrete policy remains a leaf. Unknown leaves stay unknown so
// valid_vector_policies can reject them without silently ignoring options.
template<class... Packs>
struct concat_packs;
template<> struct concat_packs<> { using type = policy_pack<>; };
template<class... P> struct concat_packs<policy_pack<P...>> {
    using type = policy_pack<P...>;
};
template<class... A, class... B, class... Rest>
struct concat_packs<policy_pack<A...>, policy_pack<B...>, Rest...>
    : concat_packs<policy_pack<A..., B...>, Rest...> {};
template<class... Packs>
using concat_packs_t = typename concat_packs<Packs...>::type;

template<class P, class = void>
struct expand_policy {
    static constexpr bool is_bundle = false;
    using type = policy_pack<P>;
};
template<class... P>
struct expand_policy<policy_pack<P...>, void> {
    static constexpr bool is_bundle = true;
    using type = concat_packs_t<typename expand_policy<P>::type...>;
};
template<class P>
struct expand_policy<P, std::void_t<typename preset_traits<P>::policies>> {
    static constexpr bool is_bundle = true;
    using type = typename expand_policy<typename preset_traits<P>::policies>::type;
};

// Layers: policies inside any bundle are defaults; directly specified leaf
// policies are explicit overrides, regardless of argument ordering.
template<class... P>
using bundle_defaults_t = concat_packs_t<
    std::conditional_t<expand_policy<P>::is_bundle,
        typename expand_policy<P>::type, policy_pack<>>...>;
template<class... P>
using direct_overrides_t = concat_packs_t<
    std::conditional_t<expand_policy<P>::is_bundle,
        policy_pack<>, typename expand_policy<P>::type>...>;

template<class Pack> struct unique_categories;
template<class... P>
struct unique_categories<policy_pack<P...>> : std::bool_constant<
    ((category_v<P> != policy_category::unknown) && ...) &&
    ((std::size_t{0} + ... + (category_v<P> == policy_category::storage)) <= 1) &&
    ((std::size_t{0} + ... + (category_v<P> == policy_category::growth)) <= 1) &&
    ((std::size_t{0} + ... + (category_v<P> == policy_category::bounds)) <= 1) &&
    ((std::size_t{0} + ... + (category_v<P> == policy_category::relocation)) <= 1) &&
    ((std::size_t{0} + ... + (category_v<P> == policy_category::allocation)) <= 1) &&
    ((std::size_t{0} + ... + (category_v<P> == policy_category::iteration)) <= 1) &&
    ((std::size_t{0} + ... + (category_v<P> == policy_category::exception_safety)) <= 1)> {};

template<class Pack> struct pack_size;
template<class... P> struct pack_size<policy_pack<P...>>
    : std::integral_constant<std::size_t, sizeof...(P)> {};

template<class Pack, class Policy> struct pack_contains;
template<class... P, class Policy>
struct pack_contains<policy_pack<P...>, Policy>
    : std::bool_constant<(std::same_as<P, Policy> || ...)> {};

// Overlay replaces matching categories instead of just appending duplicates.
// Within either layer, duplicate categories remain invalid.
template<class Policy, class Overrides> struct is_overridden;
template<class Policy, class... O>
struct is_overridden<Policy, policy_pack<O...>>
    : std::bool_constant<((category_v<Policy> == category_v<O>) || ...)> {};

template<class Defaults, class Overrides> struct overlay_packs;
template<class... D, class... O>
struct overlay_packs<policy_pack<D...>, policy_pack<O...>> {
    using type = concat_packs_t<
        std::conditional_t<is_overridden<D, policy_pack<O...>>::value,
            policy_pack<>, policy_pack<D>>...,
        policy_pack<O...>>;
};
template<class Defaults, class Overrides>
using overlay_packs_t = typename overlay_packs<Defaults, Overrides>::type;

template<policy_category Cat, class... P>
inline constexpr std::size_t policy_count_v = (std::size_t{0} + ... + (category_v<P> == Cat ? 1u : 0u));

template<policy_category Cat, class Default, class... P>
struct select_policy { using type = Default; };
template<policy_category Cat, class Default, class First, class... Rest>
struct select_policy<Cat, Default, First, Rest...> {
    using type = std::conditional_t<category_v<First> == Cat, First,
       typename select_policy<Cat, Default, Rest...>::type>;
};
template<policy_category Cat, class Default, class... P>
using select_policy_t = typename select_policy<Cat, Default, P...>::type;


template<class G>
concept callable_growth = requires(G g, std::size_t cap, std::size_t required) {
    { g(cap, required) } -> std::convertible_to<std::size_t>;
};

template<class T, class Policy> struct allocator_for;
template<class T> struct allocator_for<T, allocation::standard> { using type = std::allocator<T>; };
template<class T> struct allocator_for<T, allocation::pmr> { using type = std::pmr::polymorphic_allocator<T>; };
template<class T, class A> struct allocator_for<T, allocation::with<A>> { using type = A; };

template<class A, class T>
concept raw_pointer_allocator = requires(A a, std::size_t n) {
    typename std::allocator_traits<A>::value_type;
    requires std::same_as<typename std::allocator_traits<A>::value_type, T>;
    requires std::same_as<typename std::allocator_traits<A>::pointer, T*>;
    { std::allocator_traits<A>::allocate(a, n) } -> std::same_as<T*>;
    std::allocator_traits<A>::deallocate(a, static_cast<T*>(nullptr), n);
} && std::copy_constructible<A> && std::equality_comparable<A>;

// A transaction can roll back a failed relocation if relocation either
// cannot throw while moving, or copies without modifying its source.
template<class T, class Relocation>
inline constexpr bool transactional_relocation_v =
    std::same_as<Relocation, relocation::copy> ? std::copy_constructible<T> :
    std::same_as<Relocation, relocation::always_move> ? std::is_nothrow_move_constructible_v<T> :
    (std::is_nothrow_move_constructible_v<T> || std::copy_constructible<T>);

template<class T, class... P>
struct vector_config_flat {
    using value_type = T;
    using storage_type = select_policy_t<policy_category::storage, storage::heap, P...>;
    using growth_default = std::conditional_t<neo::is_non_growing_storage_v<storage_type>,
        growth::none, growth::geometric<>>;
    using growth_type = select_policy_t<policy_category::growth, growth_default, P...>;
    using bounds_type = select_policy_t<policy_category::bounds, bounds::unchecked, P...>;
    using relocation_type = select_policy_t<policy_category::relocation, relocation::move_if_noexcept, P...>;
    using allocation_type = select_policy_t<policy_category::allocation, allocation::standard, P...>;
    using iteration_type = select_policy_t<policy_category::iteration, iteration::raw, P...>;
    using exception_safety_type = select_policy_t<policy_category::exception_safety,
        exception_safety::basic, P...>;
    using allocator_type = typename allocator_for<T, allocation_type>::type;
    static constexpr bool valid =
        (sizeof(T) > 0) && ((category_v<P> != policy_category::unknown) && ...) &&
        policy_count_v<policy_category::storage, P...> <= 1 &&
        policy_count_v<policy_category::growth, P...> <= 1 &&
        policy_count_v<policy_category::bounds, P...> <= 1 &&
        policy_count_v<policy_category::relocation, P...> <= 1 &&
        policy_count_v<policy_category::allocation, P...> <= 1 &&
        policy_count_v<policy_category::iteration, P...> <= 1 &&
        policy_count_v<policy_category::exception_safety, P...> <= 1 &&
        (policy_count_v<policy_category::allocation, P...> == 0 ||
          !neo::is_non_growing_storage_v<storage_type>) &&
        raw_pointer_allocator<allocator_type, T> &&
        (neo::is_non_growing_storage_v<storage_type> == std::same_as<growth_type, growth::none>) &&
        (std::same_as<growth_type, growth::none> || callable_growth<growth_type>) &&
        (!std::same_as<relocation_type, relocation::copy> || std::copy_constructible<T>) &&
        (neo::is_non_growing_storage_v<storage_type> ||
         !std::same_as<exception_safety_type, exception_safety::strong_growth> ||
         transactional_relocation_v<T, relocation_type>);
};

// Feed one unambiguous, flattened policy list to the pre-existing resolver.
template<class T, class Pack> struct flat_config_from_pack;
template<class T, class... P>
struct flat_config_from_pack<T, policy_pack<P...>> : vector_config_flat<T, P...> {};

template<class T, class... P>
struct vector_config : flat_config_from_pack<T, overlay_packs_t<
    bundle_defaults_t<P...>, direct_overrides_t<P...>>> {
private:
    using defaults = bundle_defaults_t<P...>;
    using overrides = direct_overrides_t<P...>;
    using base = flat_config_from_pack<T, overlay_packs_t<defaults, overrides>>;
public:
    static constexpr bool valid = unique_categories<defaults>::value &&
        unique_categories<overrides>::value && base::valid;
};
} // namespace detail

// A reusable named customization layer. An override replaces the inherited
// policy from the same category, and duplicate overrides are rejected.
template<class Base, class... Overrides>
struct with_policies {
private:
    using inherited = typename detail::expand_policy<Base>::type;
    using explicit_policies = detail::concat_packs_t<
        typename detail::expand_policy<Overrides>::type...>;
public:
    static constexpr bool valid = detail::expand_policy<Base>::is_bundle &&
        detail::unique_categories<inherited>::value &&
        detail::unique_categories<explicit_policies>::value;
    using type = detail::overlay_packs_t<inherited, explicit_policies>;
};
template<class Base, class... Overrides>
    requires with_policies<Base, Overrides...>::valid
using with_policies_t = typename with_policies<Base, Overrides...>::type;

// Composable, zero-state presets. They are ordinary policy_pack aliases and
// can be combined, nested, or customized with with_policies_t.
namespace presets {
template<std::size_t N>
using small = policy_pack<storage::small<N>>;
template<std::size_t N>
using fixed = policy_pack<storage::fixed<N>>;
using checked = policy_pack<bounds::checked, iteration::checked_prefix>;
using pmr = policy_pack<allocation::pmr>;
template<std::size_t N>
using safe_small = policy_pack<small<N>, checked, exception_safety::strong_growth>;
template<std::size_t N>
using safe_fixed = policy_pack<fixed<N>, checked>;
template<std::size_t N>
using pmr_small = policy_pack<small<N>, pmr>;
} // namespace presets

namespace traits {
// Inspection without instantiating a vector<T> at all.
template<class Preset>
struct policy_pack_traits {
    using flattened = typename detail::expand_policy<Preset>::type;
    static constexpr bool is_bundle = detail::expand_policy<Preset>::is_bundle;
    static constexpr bool has_no_conflicts = detail::unique_categories<flattened>::value;
    static constexpr std::size_t size = detail::pack_size<flattened>::value;
    template<class Policy>
    static constexpr bool contains = detail::pack_contains<flattened, Policy>::value;
};
}

template<class T, class... Policies>
concept valid_vector_policies = detail::vector_config<T, Policies...>::valid;
}
