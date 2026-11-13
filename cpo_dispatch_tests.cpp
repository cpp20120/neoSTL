#include "neo.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <forward_list>
#include <list>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace custom {
struct Range {
    std::vector<int> values{9, 1, 6, 3, 4};
    int fast_calls = 0;
    int stable_calls = 0;
    int nth_calls = 0;
    int partition_calls = 0;

    auto begin() noexcept { return values.begin(); }
    auto end() noexcept { return values.end(); }
    auto begin() const noexcept { return values.begin(); }
    auto end() const noexcept { return values.end(); }
    auto size() const noexcept { return values.size(); }

    friend void tag_invoke(neo::sort_tag<neo::sort_mode::fast>, Range& r,
                           neo::ordering<>) noexcept {
        ++r.fast_calls;
        // Test-only custom algorithm; comparator of int does not throw.
        for (std::size_t i = 1; i < r.values.size(); ++i) {
            for (std::size_t j = i; j > 0 && r.values[j] < r.values[j - 1]; --j)
                std::swap(r.values[j], r.values[j - 1]);
        }
    }
    // Stable mode deliberately has no tag_invoke: exercises the fallback.

    friend auto tag_invoke(neo::nth_element_tag, Range& r,
                           std::size_t k, neo::ordering<>) -> std::vector<int>::iterator {
        ++r.nth_calls;
        auto nth = r.values.begin() + static_cast<std::ptrdiff_t>(k);
        std::nth_element(r.values.begin(), nth, r.values.end());
        return nth;
    }

    template<class Pred, class Proj>
    friend auto tag_invoke(neo::partition_tag<false>, Range& r, Pred pred, Proj proj)
        -> std::vector<int>::iterator {
        ++r.partition_calls;
        return std::ranges::partition(r.values, std::move(pred), std::move(proj)).begin();
    }
};

// A range whose elements have no operator<. Only the custom CPO can sort it.
struct Record { int rank{}; };
struct RecordRange {
    std::array<Record, 3> items{{{5}, {1}, {3}}};
    auto begin() noexcept { return items.begin(); }
    auto end() noexcept { return items.end(); }
    auto size() const noexcept { return items.size(); }

    friend void tag_invoke(neo::sort_tag<neo::sort_mode::fast>,
                           RecordRange& r, neo::ordering<>) {
        std::ranges::sort(r.items, std::ranges::less{}, &Record::rank);
    }
};


// A constrained customization that becomes viable only for projections making
// the element type sortable. This models third-party constrained tag_invoke.
struct Work { int priority{}; };
struct ProjectedRange {
    std::vector<Work> jobs{{8}, {1}, {5}};
    bool customized = false;
    auto begin() { return jobs.begin(); }
    auto end() { return jobs.end(); }
    template<class P, class C>
        requires std::sortable<std::vector<Work>::iterator, C, P>
    friend void tag_invoke(neo::sort_tag<neo::sort_mode::fast>,
                           ProjectedRange& r, neo::ordering<P, C> opts) {
        r.customized = true;
        std::ranges::sort(r.jobs, std::move(opts.compare), std::move(opts.projection));
    }
};

struct BadReturnRange {
    std::array<Record, 1> items{{{9}}};
    auto begin() noexcept { return items.begin(); }
    auto end() noexcept { return items.end(); }
    friend int tag_invoke(neo::sort_tag<neo::sort_mode::fast>,
                          BadReturnRange&, neo::ordering<>) { return 0; }
};

struct Buffer {
    std::vector<int> values{};
    bool custom_append_called = false;
    bool custom_reserve_called = false;
    template<std::ranges::input_range R>
    void append_range(R&& r) {
        for (auto&& x : r) values.push_back(x);
    }
    void reserve(std::size_t n) { values.reserve(n); }

    template<std::ranges::input_range R>
    friend void tag_invoke(neo::append_range_tag, Buffer& out, R&& r) {
        out.custom_append_called = true;
        for (auto&& x : r) out.values.push_back(x);
    }
    friend void tag_invoke(neo::reserve_tag, Buffer& out, std::size_t n) {
        out.custom_reserve_called = true;
        out.values.reserve(n);
    }
};
} // namespace custom

using StandardVector = neo::vector<int>;
static_assert(neo::custom_sortable_range<neo::sort_mode::fast, custom::Range&>);
static_assert(neo::sort_dispatchable<neo::sort_mode::fast, custom::Range&>);
static_assert(neo::sort_dispatchable<neo::sort_mode::stable, custom::Range&>);
static_assert(noexcept(neo::sort(std::declval<custom::Range&>())));
static_assert(!noexcept(neo::sort(std::declval<custom::Range&>(), neo::stable)));
static_assert(neo::custom_nth_selectable_range<custom::Range&>);
static_assert(neo::custom_partitionable_range<false, custom::Range&, decltype([](int){ return true; })>);
static_assert(!neo::mutable_sortable_range<custom::RecordRange&>);
static_assert(neo::sort_dispatchable<neo::sort_mode::fast, custom::RecordRange&>);
static_assert(!neo::sort_dispatchable<neo::sort_mode::fast, custom::BadReturnRange&>);
static_assert(!neo::sort_dispatchable<neo::sort_mode::fast, custom::ProjectedRange&>);
static_assert(neo::sort_dispatchable<neo::sort_mode::fast, custom::ProjectedRange&,
    decltype(&custom::Work::priority)>);
static_assert(!noexcept(neo::reserve(std::declval<custom::Buffer&>(), std::size_t{})));
static_assert(neo::tag_invocable<neo::sort_tag<neo::sort_mode::fast>, custom::Range&,
                                  neo::ordering<>>);
static_assert(neo::nothrow_tag_invocable<neo::sort_tag<neo::sort_mode::fast>, custom::Range&,
                                          neo::ordering<>>);
static_assert(neo::appendable_range<StandardVector, std::array<int, 2>&>);
static_assert(neo::appendable_range<std::vector<int>, std::array<int, 2>&>);
static_assert(neo::reservable_container<StandardVector>);
static_assert(neo::reservable_container<std::vector<int>>);
static_assert(neo::range_constructible<StandardVector, std::array<int, 2>&>);
static_assert(neo::reversible_range<std::list<int>&>);
static_assert(!neo::reversible_range<std::forward_list<int>&>);
static_assert(neo::allocator_compatible<StandardVector, neo::vector<int, neo::presets::small<4>>>);
static_assert(!neo::allocator_compatible<StandardVector, neo::vector<int, neo::allocation::pmr>>);
static_assert(neo::allocator_equality_comparable<std::allocator<int>, std::allocator<int>>);
static_assert(neo::statically_interchangeable_allocators<std::allocator<int>, std::allocator<int>>);
static_assert(!neo::statically_interchangeable_allocators<
    std::pmr::polymorphic_allocator<int>, std::pmr::polymorphic_allocator<int>>);
static_assert(neo::transactionally_growable<neo::vector<int, neo::presets::safe_small<8>>>);
static_assert(!neo::transactionally_growable<neo::vector<int, neo::presets::fixed<8>>>);

int main() {
    custom::Range r;
    neo::sort(r);
    assert(r.fast_calls == 1);
    assert(std::ranges::is_sorted(r));

    r.values = {5, 2, 9, 1, 4};
    neo::sort<neo::sort_mode::stable>(r);
    assert(r.fast_calls == 1 && r.stable_calls == 0);
    assert(std::ranges::is_sorted(r));
    neo::sort(r, neo::stable);

    r.values = {9, 2, 3, 1, 4};
    auto nth = neo::nth_element<2>(r);
    assert(nth == r.begin() + 2 && *nth == 3 && r.nth_calls == 1);
    bool threw = false;
    try { (void)neo::nth_element(r, 500); }
    catch (const std::out_of_range&) { threw = true; }
    assert(threw && r.nth_calls == 1);

    r.values = {1, 2, 3, 4};
    auto pivot = neo::partition(r, [](int x) { return x % 2 == 0; });
    assert(r.partition_calls == 1 && pivot == r.begin() + 2);
    assert(std::ranges::all_of(std::ranges::subrange(r.begin(), pivot),
                               [](int x){ return x % 2 == 0; }));

    custom::ProjectedRange pr;
    neo::sort(pr, neo::by(&custom::Work::priority));
    assert(pr.customized && pr.jobs.front().priority == 1);

    custom::RecordRange rec;
    neo::sort(rec);
    assert(rec.items[0].rank == 1 && rec.items[2].rank == 5);

    neo::vector<int> neo_values{9, 2, 5};
    neo::sort(neo_values, neo::stable);
    auto kth = neo::nth_element(neo_values, 1);
    assert(*kth == 5);
    auto boundary = neo::partition(neo_values, [](int x) { return x > 5; });
    assert(boundary == neo_values.begin() + 1);

    std::array data{1, 2, 3};
    custom::Buffer b;
    neo::append_range(b, data);
    assert(b.custom_append_called && (b.values == std::vector{1, 2, 3}));
    neo::reserve(b, 100);
    assert(b.custom_reserve_called && b.values.capacity() >= 100);

    neo::vector<int> nv;
    neo::append_range(nv, data); // member fallback
    assert(std::ranges::equal(nv, data));
    neo::reserve(nv, 30);
    assert(nv.capacity() >= 30);

    std::vector<int> sv;
    neo::append_range(sv, data); // generic emplace_back fallback
    assert(std::ranges::equal(sv, data));
    neo::reserve(sv, 30);
    assert(sv.capacity() >= 30);

    std::forward_list<int> fwd{1, 2, 3, 4};
    auto cut = neo::partition(fwd, [](int x){ return x % 2 == 0; });
    assert(std::distance(fwd.begin(), cut) == 2);
}
