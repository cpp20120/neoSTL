#include "neo.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <forward_list>
#include <list>
#include <ranges>
#include <sstream>
#include <span>
#include <vector>

using Pred = bool (*)(int);
using L = std::forward_list<int>;
using V = neo::vector<int, neo::iteration::checked_prefix>;
using Uni = decltype(neo::views::unified(std::declval<V&>(), 2, 3));
using Bidi = decltype(neo::views::unified(std::declval<std::list<int>&>(), 2, 3));
using Fwd = decltype(neo::views::unified(std::declval<L&>(), 2, 3));
using Input = decltype(std::ranges::istream_view<int>(std::declval<std::istream&>()));

static_assert(neo::viewable_input_range<V&>);
static_assert(!neo::viewable_input_range<int>);
static_assert(neo::input_only_range<Input>);
static_assert(neo::forward_only_range<L>);
static_assert(neo::bidirectional_only_range<Bidi>);
static_assert(neo::random_access_only_range<Uni>);
static_assert(neo::common_bidirectional_range<Bidi>);
static_assert(neo::sized_random_access_range<Uni>);
static_assert(neo::borrowed_contiguous_range<std::span<int>>);
static_assert(!neo::borrowed_contiguous_range<V>);
static_assert(neo::const_iterable_range<std::span<int>>);
static_assert(!neo::const_iterable_range<Input>);
static_assert(neo::mutable_forward_range<V&>);
static_assert(!neo::mutable_forward_range<Uni>);
static_assert(!neo::mutable_forward_range<std::span<const int>>);
static_assert(neo::traits::range_traits<Uni>::random_access_only);
static_assert(neo::traits::range_traits<Fwd>::forward_only);
static_assert(neo::traits::range_traits<Bidi>::bidirectional_only);
static_assert(neo::traits::range_traits<V&>::mutable_forward);

static_assert(neo::partitionable_range<L&, Pred>);
static_assert(neo::in_place_partitionable_range<L&, Pred>); // compatibility spelling
static_assert(!neo::stable_partitionable_range<L&, Pred>);
static_assert(neo::stable_partitionable_range<std::list<int>&, Pred>);
static_assert(!neo::partitionable_range<std::span<const int>, Pred>);
static_assert(!neo::partitionable_range<L, Pred>); // rvalue owner -> dangling
static_assert(neo::rotatable_range<L&>);
static_assert(neo::unique_compactable_range<L&>);
static_assert(!neo::unique_compactable_range<Uni>); // repeated read-only references
static_assert(neo::binary_searchable_range<L&, int>);
static_assert(neo::partial_sortable_range<V&>);
static_assert(!neo::partial_sortable_range<L&>);

struct Item { int key; };
static_assert(neo::binary_searchable_range<std::vector<Item>&, int,
              std::ranges::less, decltype(&Item::key)>);

int main() {
    L values{1, 4, 3, 6, 5, 2};
    auto first_false = neo::partition(values, [](int x) { return x % 2 == 1; });
    assert(std::ranges::all_of(std::ranges::subrange(values.begin(), first_false),
        [](int x) { return x % 2 == 1; }));
    assert(std::ranges::none_of(std::ranges::subrange(first_false, values.end()),
        [](int x) { return x % 2 == 1; }));

    std::list<int> list{1, 2, 3, 4};
    auto boundary = neo::partition(list, neo::stable, [](int x) { return x % 2 == 0; });
    assert(*boundary == 1);
    assert((std::ranges::equal(list, std::array{2,4,1,3})));

    L rotating{1,2,3,4,5};
    auto mid = std::ranges::next(rotating.begin(), 2);
    auto rotated_first = neo::rotate(rotating, mid);
    assert(*rotated_first == 1);
    assert((std::ranges::equal(rotating, std::array{3,4,5,1,2})));

    L adjacent{1,1,2,2,2,3,3};
    auto compacted = neo::unique(adjacent);
    assert((std::ranges::equal(std::ranges::subrange(adjacent.begin(), compacted.begin()),
        std::array{1,2,3})));

    std::vector<Item> sorted{{1},{1},{3},{8},{8}};
    auto it = neo::lower_bound(sorted, 3, neo::by(&Item::key));
    assert(it->key == 3);
}
