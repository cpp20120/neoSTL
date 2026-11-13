#include "neo.hpp"
#include <array>
#include <cassert>
#include <forward_list>
#include <ranges>
#include <memory_resource>
#include <new>
#include <type_traits>

// Integration check: failed transactional growth does not invalidate even a
// checked-prefix iterator; a successful reallocation does invalidate it.
struct FailingResource : std::pmr::memory_resource {
    bool fail = false;
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (fail) throw std::bad_alloc{};
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};


struct AlwaysTrue { bool operator()(int) const noexcept { return true; } };
using Heap = neo::vector<int>;
using Fixed = neo::vector<int, neo::storage::fixed<3>>;
using Prefix = neo::vector<int, neo::storage::small<8>, neo::iteration::checked_prefix,
                           neo::exception_safety::strong_growth>;
static_assert(neo::growing_policy_vector<Heap>);
static_assert(neo::non_growing_policy_vector<Fixed>);
static_assert(!neo::growing_policy_vector<Fixed>);
static_assert(neo::inline_buffer_vector<Fixed>);
static_assert(!neo::inline_buffer_vector<Heap>);
static_assert(neo::checked_iteration_vector<Prefix>);
static_assert(neo::prefix_preserving_append_vector<Prefix>);
static_assert(!neo::prefix_preserving_append_vector<Heap>);
static_assert(neo::strong_growth_vector<Prefix>);
static_assert(!neo::strong_growth_vector<Heap>);
static_assert(neo::transactionally_relocatable<int, neo::relocation::always_move>);
static_assert(neo::statically_reservable_vector<Fixed, 3>);
static_assert(!neo::statically_reservable_vector<Fixed, 4>);
static_assert(!neo::statically_reservable_vector<neo::vector<int, neo::storage::external>, 1>);
static_assert(neo::mutable_sortable_range<Prefix&>);
static_assert(!neo::mutable_sortable_range<const Prefix&>);
static_assert(neo::nth_selectable_range<Prefix&>);
static_assert(!neo::nth_selectable_range<Prefix>); // rvalue would return dangling iterator
static_assert(neo::in_place_partitionable_range<Prefix&, AlwaysTrue>);
static_assert(neo::in_place_partitionable_range<std::forward_list<int>&, AlwaysTrue>);
static_assert(neo::partitionable_range<std::forward_list<int>&, AlwaysTrue>);
static_assert(!neo::stable_partitionable_range<std::forward_list<int>&, AlwaysTrue>);

// Bad compositions fail as a constraint, not deep inside a member body.
static_assert(!neo::valid_vector_policies<int, neo::growth::exact, neo::growth::geometric<>>);
static_assert(!neo::valid_vector_policies<int, neo::storage::fixed<3>, neo::growth::exact>);
static_assert(!neo::valid_vector_policies<int, neo::iteration::raw, neo::iteration::checked_prefix>);
static_assert(!neo::valid_vector_policies<int, neo::exception_safety::basic,
    neo::exception_safety::strong_growth>);
static_assert(!neo::valid_vector_policies<int, neo::allocation::with<std::allocator<double>>>);

int main() {
    FailingResource resource;
    using Guarded = neo::vector<int, neo::allocation::pmr,
        neo::exception_safety::strong_growth, neo::iteration::checked_prefix>;
    Guarded guarded{&resource};
    guarded.emplace_back(11);
    auto iter = guarded.begin();
    auto* address = guarded.data();
    const auto capacity = guarded.capacity();
    resource.fail = true;
    bool threw = false;
    try { guarded.emplace_back(22); } catch (const std::bad_alloc&) { threw = true; }
    assert(threw);
    assert(guarded.data() == address && guarded.capacity() == capacity && guarded.size() == 1);
    assert(*iter == 11);
    resource.fail = false;
    guarded.emplace_back(22);
    bool invalidated = false;
    try { (void)*iter; } catch (const std::logic_error&) { invalidated = true; }
    assert(invalidated);

    Prefix values;
    values.emplace_back(9);
    values.emplace_back(3);
    values.emplace_back(7);
    neo::sort(values);
    assert(values[0] == 3);
    auto mid = neo::nth_element(values, 1);
    assert(*mid == 7);
    auto boundary = neo::partition(values, AlwaysTrue{});
    assert(boundary == values.end());
}
