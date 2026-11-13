#include "neo.hpp"
#include <array>
#include <cassert>
#include <concepts>
#include <ranges>
#include <stdexcept>
#include <type_traits>

struct user_debug_preset {};
template<>
struct neo::preset_traits<user_debug_preset> {
    using policies = neo::policy_pack<
        neo::storage::small<8>,
        neo::bounds::checked,
        neo::iteration::checked_prefix>;
};

using nested = neo::policy_pack<
    neo::policy_pack<neo::storage::small<8>, neo::bounds::checked>,
    neo::iteration::checked_prefix>;
using tuned = neo::with_policies_t<nested,
    neo::bounds::unchecked,
    neo::growth::exact>;
using personalized = neo::vector<int, user_debug_preset, neo::bounds::unchecked>;
using right_order = neo::vector<int, neo::bounds::unchecked, user_debug_preset>;
using tuned_vector = neo::vector<int, tuned>;

static_assert(std::same_as<typename personalized::policy_config::bounds_type,
                           neo::bounds::unchecked>);
static_assert(std::same_as<typename right_order::policy_config::bounds_type,
                           neo::bounds::unchecked>);
static_assert(std::same_as<typename tuned_vector::policy_config::storage_type,
                           neo::storage::small<8>>);
static_assert(std::same_as<typename tuned_vector::policy_config::growth_type,
                           neo::growth::exact>);
static_assert(!neo::traits::vector_traits<tuned_vector>::is_checked_bounds);
static_assert(neo::traits::vector_traits<tuned_vector>::preserves_append_prefix);
static_assert(neo::traits::policy_pack_traits<nested>::is_bundle);
static_assert(neo::traits::policy_pack_traits<nested>::has_no_conflicts);
static_assert(neo::traits::policy_pack_traits<nested>::size == 3);
static_assert(neo::traits::policy_pack_traits<nested>::contains<neo::bounds::checked>);
static_assert(neo::traits::policy_pack_traits<tuned>::contains<neo::bounds::unchecked>);
static_assert(!neo::traits::policy_pack_traits<tuned>::contains<neo::bounds::checked>);
static_assert(neo::valid_vector_policies<int, user_debug_preset>);
static_assert(neo::valid_vector_policies<int, user_debug_preset, neo::bounds::unchecked>);
static_assert(!neo::valid_vector_policies<int, neo::bounds::unchecked, neo::bounds::checked>);
static_assert(!neo::valid_vector_policies<int,
    neo::policy_pack<neo::bounds::checked, neo::bounds::unchecked>>);
static_assert(!neo::valid_vector_policies<int,
    neo::policy_pack<neo::bounds::checked>, neo::policy_pack<neo::bounds::unchecked>>);
static_assert(!neo::with_policies<nested, neo::bounds::checked,
                                      neo::bounds::unchecked>::valid);
static_assert(!neo::with_policies<neo::bounds::checked, neo::bounds::unchecked>::valid);
// If the base does not specify growth, changing storage inherits the
// appropriate default for the new storage class.
static_assert(neo::valid_vector_policies<int,
    neo::with_policies_t<nested, neo::storage::fixed<8>>>);
using fixed = neo::with_policies_t<nested, neo::storage::fixed<8>, neo::growth::none>;
static_assert(neo::valid_vector_policies<int, fixed>);

int main() {
    personalized p;
    p.emplace_back(2);
    p.emplace_back(1);
    neo::sort(p);
    assert(p[0] == 1 && p[1] == 2);

    auto v = std::ranges::to<tuned_vector>(std::views::iota(0, 12));
    assert(v.size() == 12);
    assert(v[11] == 11);
    assert(v.capacity() == 12); // exact growth policy
    assert(v.get_allocator() == std::allocator<int>{});

    neo::vector<int, fixed> f;
    f.append_range(std::array{3, 2, 1});
    neo::sort(f);
    assert(f[0] == 1);
    assert(f.capacity() == 8);
}
