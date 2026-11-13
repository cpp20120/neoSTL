#include "neo.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#define CHECK(...) do { if (!(__VA_ARGS__)) throw std::runtime_error("FAIL: " #__VA_ARGS__); } while(false)
#include <forward_list>
#include <iterator>
#include <list>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace neo;
using neo::traits::vector_traits;

using small = vector<int, storage::small<4>, bounds::checked, iteration::checked>;
using fixed = vector<int, storage::fixed<4>>;
using external = vector<int, storage::external>;
using heap = vector<int>;

static_assert(is_non_growing_storage_v<storage::fixed<4>>);
static_assert(is_non_growing_storage_v<storage::external>);
static_assert(!is_non_growing_storage_v<storage::small<4>>);
static_assert(!is_non_growing_storage_v<storage::heap>);
static_assert(non_growing_storage<storage::fixed<1>>);
static_assert(growing_storage<storage::small<0>>);
static_assert(traits::has_inline_storage_v<storage::fixed<4>>);
static_assert(traits::has_inline_storage_v<storage::small<4>>);
static_assert(!traits::has_inline_storage_v<storage::external>);
static_assert(traits::is_borrowed_storage_v<storage::external>);
static_assert(traits::storage_traits<storage::small<4>>::inline_capacity == 4);
static_assert(traits::storage_traits<storage::fixed<4>>::has_compile_time_capacity);
static_assert(!traits::storage_traits<storage::external>::has_compile_time_capacity);
static_assert(traits::growth_traits<growth::geometric<>>::is_nothrow);
static_assert(traits::growth_traits<growth::none>::is_nothrow);
static_assert(!traits::growth_traits<growth::none>::enabled);
static_assert(traits::bounds_traits<bounds::checked>::is_checked);
static_assert(traits::iteration_traits<iteration::checked>::invalidation_is_detected);
static_assert(traits::relocation_traits<int, relocation::move_if_noexcept>::is_nothrow);
static_assert(traits::relocation_traits<int, relocation::always_move>::uses_move);
static_assert(!traits::relocation_traits<int, relocation::copy>::uses_move);
static_assert(traits::allocation_traits<int, allocation::pmr>::is_pmr);
static_assert(!traits::allocation_traits<int, allocation::standard>::is_pmr);
static_assert(policy_vector<small>);
static_assert(policy_vector<const fixed&>);
static_assert(!policy_vector<int>);
static_assert(vector_traits<small>::can_grow);
static_assert(vector_traits<small>::has_inline_buffer);
static_assert(vector_traits<small>::is_checked_iteration);
static_assert(vector_traits<small>::is_checked_bounds);
static_assert(vector_traits<small>::inline_capacity == 4);
static_assert(!vector_traits<small>::has_compile_time_capacity);
static_assert(!vector_traits<fixed>::can_grow);
static_assert(vector_traits<fixed>::has_compile_time_capacity);
static_assert(!vector_traits<external>::owns_buffer);
static_assert(std::same_as<vector_traits<small>::growth_policy, growth::geometric<>>);
static_assert(std::same_as<vector_traits<fixed>::growth_policy, growth::none>);

static_assert(noexcept(heap{}));
static_assert(noexcept(std::declval<heap&>()[0]));
static_assert(!noexcept(std::declval<small&>()[0]));
static_assert(noexcept(std::declval<fixed&>().clear()));
static_assert(noexcept(std::declval<fixed&>().template reserve_static<4>()));
static_assert(!noexcept(std::declval<heap&>().template reserve_static<4>()));
static_assert(noexcept(std::declval<heap&>().get_allocator()));
static_assert(noexcept(by(&std::pair<int, int>::first)));
static_assert(traits::ordering_traits<decltype(by(&std::pair<int, int>::first))>::is_identity_projection == false);

using ra = decltype(views::unified(std::declval<std::vector<int>&>(), 2, 3));
using bi = decltype(views::unified(std::declval<std::list<int>&>(), 2, 3));
using fw = decltype(views::unified(std::declval<std::forward_list<int>&>(), 2, 3));
static_assert(std::ranges::random_access_range<ra>);
static_assert(std::ranges::bidirectional_range<bi>);
static_assert(std::ranges::common_range<bi>);
static_assert(!std::ranges::random_access_range<bi>);
static_assert(std::ranges::forward_range<fw>);
static_assert(!std::ranges::bidirectional_range<fw>);
static_assert(!std::ranges::common_range<fw>);
static_assert(forward_only_range<fw>);
static_assert(traits::range_traits<fw>::forward_only);
static_assert(traits::range_traits<bi>::bidirectional);
static_assert(traits::is_unified_view_v<ra>);
static_assert(!traits::is_unified_view_v<std::vector<int>>);
static_assert(traits::unified_view_traits<bi>::bidirectional);
static_assert(traits::unified_view_traits<fw>::forward_only);
static_assert(traits::unified_view_traits<ra>::read_only);

struct StrongCopy {
    StrongCopy() = default;
    StrongCopy(const StrongCopy&) noexcept = default;
    StrongCopy(StrongCopy&&) noexcept(false) {}
};
static_assert(!traits::relocation_traits<StrongCopy, relocation::move_if_noexcept>::uses_move);
static_assert(traits::relocation_traits<StrongCopy, relocation::move_if_noexcept>::is_nothrow);
static_assert(!traits::relocation_traits<StrongCopy, relocation::always_move>::is_nothrow);

// A bidirectional *iterator* does not imply a common_range: the view
// deliberately downgrades this case to forward+sentinel rather than promising
// --end() when no end iterator exists.
struct list_sentinel {
    std::list<int>::iterator tail{};
    friend bool operator==(const std::list<int>::iterator& it, const list_sentinel& sent) {
        return it == sent.tail;
    }
};
struct noncommon_list : std::ranges::view_interface<noncommon_list> {
    std::list<int>* source = nullptr;
    noncommon_list() = default;
    explicit noncommon_list(std::list<int>& list) : source(&list) {}
    auto begin() const { return source->begin(); }
    auto end() const { return list_sentinel{source->end()}; }
};
static_assert(std::ranges::bidirectional_range<noncommon_list>);
static_assert(!std::ranges::common_range<noncommon_list>);
using fallback = decltype(views::unified(std::declval<noncommon_list>(), 2, 3));
static_assert(std::ranges::forward_range<fallback>);
static_assert(!std::ranges::bidirectional_range<fallback>);
static_assert(!std::ranges::common_range<fallback>);
static_assert(traits::unified_view_traits<fallback>::forward_only);

void test_bidi() {
    for (int n = 0; n < 15; ++n) {
        std::list<int> list;
        for (int i = 0; i < n; ++i) list.push_back(i);
        for (int step = 1; step <= 18; ++step) {
            for (int repeat = 1; repeat <= 5; ++repeat) {
                auto v = list | views::unified(step, repeat);
                std::vector<int> expected;
                for (int i = 0; i < n; i += step)
                    for (int j = 0; j < repeat; ++j) expected.push_back(i);
                CHECK(std::ranges::equal(v, expected));
                CHECK(std::ranges::distance(v.begin(), v.end()) ==
                       static_cast<std::ptrdiff_t>(expected.size()));
                auto first = v.begin();
                auto last = v.end();
                if (!expected.empty()) {
                    auto it = last;
                    for (auto rit = expected.rbegin(); rit != expected.rend(); ++rit) {
                        --it;
                        CHECK(*it == *rit);
                    }
                    CHECK(it == first);
                    auto it2 = first;
                    for (int x : expected) {
                        CHECK(*it2 == x);
                        ++it2;
                    }
                    CHECK(it2 == last);
                    auto last_element = last;
                    CHECK(*--last_element == expected.back());
                } else {
                    CHECK(first == last);
                }
                auto const& cv = v;
                CHECK(std::ranges::equal(cv, expected));
            }
        }
    }
}

void test_noncommon_bidi() {
    std::list<int> source{0,1,2,3,4,5,6};
    auto view = noncommon_list{source} | views::unified(3, 2);
    CHECK((std::ranges::equal(view, std::array{0,0,3,3,6,6})));
    CHECK(std::ranges::distance(view) == 6);
}

void test_forward() {
    std::forward_list<int> list{1,2,3,4,5};
    auto view = list | views::unified(2,2);
    CHECK((std::ranges::equal(view, std::array{1,1,3,3,5,5})));
    auto before = view.begin();
    auto after = before;
    ++after;
    CHECK(*before == 1 && *after == 1);
}

void test_storage() {
    fixed a;
    a.reserve_static<4>();
    a.emplace_back(1);
    a.emplace_back(2);
    CHECK((std::ranges::equal(a, std::array{1,2})));
    small b;
    b.emplace_back(7);
    b.emplace_back(3);
    CHECK(b.using_inline_storage());
    b.reserve(10);
    CHECK(!b.using_inline_storage());
    neo::sort(b);
    CHECK((std::ranges::equal(b, std::array{3,7})));
}

int main() {
    test_bidi();
    test_forward();
    test_noncommon_bidi();
    test_storage();
}
