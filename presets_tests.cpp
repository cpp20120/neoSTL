#include "neo.hpp"
#include <array>
#include <cassert>
#include <concepts>
#include <memory_resource>
#include <ranges>
#include <type_traits>

using ready = neo::presets::safe_small<8>;
using tuned = neo::with_policies_t<ready,
    neo::bounds::unchecked,
    neo::growth::exact,
    neo::presets::pmr>;
using tuned_vector = neo::vector<int, tuned>;
using explicit_vector = neo::vector<int,
    neo::presets::small<8>, neo::presets::checked, neo::growth::exact>;
using simple = neo::vector<int, neo::presets::small<8>>;
using fixed = neo::vector<int, neo::presets::safe_fixed<4>>;

static_assert(neo::valid_vector_policies<int, ready>);
static_assert(neo::valid_vector_policies<int, tuned>);
static_assert(neo::valid_vector_policies<int, neo::presets::pmr_small<8>>);
static_assert(neo::valid_vector_policies<int, neo::presets::safe_fixed<8>>);
static_assert(!neo::valid_vector_policies<int,
    neo::presets::safe_small<8>, neo::presets::checked>); // conflicting bundles
static_assert(!neo::valid_vector_policies<int,
    neo::presets::fixed<4>, neo::growth::geometric<>>);
static_assert(!neo::valid_vector_policies<int,
    neo::policy_pack<neo::presets::pmr, neo::presets::pmr>>);
static_assert(neo::traits::vector_traits<tuned_vector>::has_inline_buffer);
static_assert(neo::traits::vector_traits<tuned_vector>::strong_growth_guarantee);
static_assert(!neo::traits::vector_traits<tuned_vector>::is_checked_bounds);
static_assert(neo::traits::vector_traits<tuned_vector>::preserves_append_prefix);
static_assert(std::same_as<typename tuned_vector::policy_config::growth_type,
                           neo::growth::exact>);
static_assert(std::same_as<typename tuned_vector::policy_config::allocation_type,
                           neo::allocation::pmr>);
static_assert(neo::traits::policy_pack_traits<ready>::size == 4);
static_assert(neo::traits::policy_pack_traits<tuned>::size == 6);
static_assert(neo::traits::vector_traits<fixed>::has_compile_time_capacity);
static_assert(!neo::traits::vector_traits<fixed>::can_grow);

int main() {
    auto small = std::array{3, 1, 4, 2} | std::ranges::to<simple>();
    neo::sort(small);
    assert(small[0] == 1 && small[3] == 4);
    assert(small.using_inline_storage());

    std::byte backing[1024]{};
    std::pmr::monotonic_buffer_resource resource{backing, sizeof(backing)};
    tuned_vector v{&resource};
    v.append_range(std::views::iota(0, 16));
    assert(v.size() == 16);
    assert(v.capacity() == 16); // exact growth after SBO spillover
    assert(!v.using_inline_storage());
    assert(v.get_allocator().resource() == &resource);

    explicit_vector e;
    e.emplace_back(5);
    auto it = e.begin();
    e.emplace_back(9); // no reallocation, checked_prefix preserved
    assert(*it == 5);

    fixed f;
    f.append_range(std::array{3, 2, 1});
    neo::sort(f);
    assert(f.front() == 1);
}
