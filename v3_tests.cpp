#include "neo.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <forward_list>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <numeric>
#include <random>
#include <sstream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#define CHECK(...) do { if (!(__VA_ARGS__)) throw std::runtime_error("FAIL: " #__VA_ARGS__); } while(false)
template<class F> void throws(F f) { bool yes=false; try { f(); } catch(const std::exception&) {yes=true;} CHECK(yes); }

template<class T>
struct CountingAlloc {
    using value_type = T;
    int* counts = nullptr;
    CountingAlloc() = default;
    explicit CountingAlloc(int* ptr) : counts(ptr) {}
    template<class U> CountingAlloc(const CountingAlloc<U>& other) : counts(other.counts) {}
    T* allocate(std::size_t n) { if (counts) ++counts[0]; return std::allocator<T>{}.allocate(n); }
    void deallocate(T* ptr, std::size_t n) noexcept { if (counts) ++counts[1]; std::allocator<T>{}.deallocate(ptr, n); }
    bool operator==(const CountingAlloc&) const = default;
};

static_assert(neo::valid_vector_policies<int, neo::allocation::pmr>);
static_assert(neo::valid_vector_policies<int, neo::iteration::checked, neo::allocation::pmr, neo::bounds::checked>);
static_assert(neo::valid_vector_policies<int, neo::allocation::with<CountingAlloc<int>>>);
static_assert(!neo::valid_vector_policies<int, neo::allocation::standard, neo::allocation::pmr>);
static_assert(!neo::valid_vector_policies<int, neo::storage::fixed<2>, neo::allocation::pmr>);
static_assert(!neo::valid_vector_policies<int, neo::allocation::with<std::allocator<double>>>);
static_assert(!neo::valid_vector_policies<int, neo::iteration::raw, neo::iteration::checked>);
static_assert(std::ranges::contiguous_range<neo::vector<int, neo::iteration::checked>>);
static_assert(std::ranges::random_access_range<neo::vector<int, neo::iteration::checked>>);
static_assert(std::contiguous_iterator<neo::vector<int, neo::iteration::checked>::iterator>);
static_assert(std::contiguous_iterator<neo::vector<int, neo::iteration::checked>::const_iterator>);

void allocator_tests() {
    int counts[2]{};
    using vec = neo::vector<int, neo::allocation::with<CountingAlloc<int>>, neo::storage::small<2>>;
    {
        vec a{std::allocator_arg, CountingAlloc<int>{counts}, {4, 2}};
        CHECK(a.size() == 2);
        a.clear();
        CHECK(a.get_allocator().counts == counts);
        a.emplace_back(4); a.emplace_back(2); CHECK(a.using_inline_storage());
        a.emplace_back(7); CHECK(!a.using_inline_storage() && counts[0] == 1);
        auto p = a.data();
        vec b{std::move(a)};
        CHECK(b.data() == p && a.empty());
        CHECK(b.size() == 3);
        neo::sort(b);
        CHECK((std::ranges::equal(b, std::array{2,4,7})));
    }
    CHECK(counts[0] == counts[1]);
    {
        int c1[2]{}, c2[2]{};
        using heap = neo::vector<int,neo::allocation::with<CountingAlloc<int>>>;
        {
            heap a{CountingAlloc<int>{c1}};
            heap b{CountingAlloc<int>{c2}};
            a.emplace_back(10); a.emplace_back(20);
            b.emplace_back(99);
            b = std::move(a); // allocators unequal: cannot steal allocation
            CHECK(b.size() == 2 && b[0] == 10 && b[1] == 20 && a.empty());
            CHECK(b.get_allocator().counts == c2);
        }
        CHECK(c1[0] == c1[1] && c2[0] == c2[1]);
    }
    {
        std::array<std::byte, 4096> buffer{};
        std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size(), std::pmr::null_memory_resource());
        neo::vector<int,neo::allocation::pmr> values{&resource};
        values.emplace_back(4);
        values.emplace_back(2);
        CHECK(values.get_allocator().resource() == &resource);
        neo::sort(values);
        CHECK((std::ranges::equal(values, std::array{2,4})));
    }
}

void iterator_tests() {
    neo::vector<int,neo::iteration::checked,neo::bounds::checked,neo::storage::small<3>> x;
    x.emplace_back(3); x.emplace_back(1); x.emplace_back(2);
    auto old = x.begin();
    CHECK(*old == 3 && old[1] == 1);
    CHECK(*x.rbegin() == 2);
    CHECK((std::ranges::equal(x | std::views::reverse, std::array{2,1,3})));
    auto p = x.begin();
    CHECK(std::to_address(p) == x.data());
    CHECK(x.end()-x.begin() == 3);
    CHECK((x.end() - 1)[0] == 2);
    CHECK(*x.cbegin() == 3);
    neo::sort(x);
    CHECK((std::ranges::equal(x, std::array{1,2,3})));
    auto first = x.begin();
    x.emplace_back(4); // spill and invalidate
    throws([&]{ (void)*first; });
    throws([&]{ (void)(first == x.begin()); });
    throws([&]{ (void)*x.end(); });
    throws([&]{ (void)*(x.begin()-1); });
    throws([&]{ (void)(x.begin()+99); });
    neo::vector<int,neo::iteration::checked> other;
    other.emplace_back(7);
    throws([&]{ (void)(other.begin() - x.begin()); });
    auto split = neo::partition(x, [](int v){return v % 2 == 0;});
    CHECK(split-x.begin() == 2);
    CHECK((std::ranges::all_of(x.begin(),split,[](int v){return v%2==0;})));
    CHECK(neo::nth_element(x, 1) - x.begin() == 1);
    const auto& cx = x;
    CHECK(std::ranges::distance(cx) == 4);
}

static_assert(std::ranges::random_access_range<decltype(neo::views::unified(std::declval<std::vector<int>&>(), 2, 3))>);
static_assert(std::ranges::sized_range<decltype(neo::views::unified(std::declval<std::vector<int>&>(), 2, 3))>);
static_assert(std::ranges::forward_range<decltype(neo::views::unified(std::declval<std::forward_list<int>&>(), 2, 3))>);
static_assert(!std::ranges::random_access_range<decltype(neo::views::unified(std::declval<std::forward_list<int>&>(), 2, 3))>);

void view_tests() {
    neo::vector values{1,2,3,4,5,6};
    auto view = values | neo::views::unified(2, 3);
    CHECK(view.size() == 9);
    CHECK((std::ranges::equal(view, std::array{1,1,1,3,3,3,5,5,5})));
    CHECK(*(view.begin()+4) == 3);
    CHECK(*(view.end()-1) == 5);
    CHECK(view.end() - view.begin() == 9);
    const auto& cv = view;
    CHECK(*(cv.begin()) == 1); // view_interface inherited cbegin C++23
    auto from_view = neo::vector{neo::from_range, view};
    CHECK(from_view.size() == 9);
    static_assert(!std::sortable<std::ranges::iterator_t<decltype(view)>>);
    auto direct = neo::views::unified(values, 3, 2);
    CHECK((std::ranges::equal(direct, std::array{1,1,4,4})));
    auto typed = values | neo::views::unified_c<4, 2>;
    CHECK((std::ranges::equal(typed, std::array{1,1,5,5})));
    auto empty = neo::views::unified(std::array<int,0>{}, 4, 2);
    CHECK(empty.size() == 0 && empty.begin() == empty.end());
    auto single = neo::views::unified(std::array{9}, 7, 2);
    CHECK((std::ranges::equal(single,std::array{9,9})));
    auto large_step = neo::views::unified(values, 999, 1);
    CHECK((std::ranges::equal(large_step,std::array{1})));
    throws([&]{ (void)neo::views::unified(values, 0, 2); });
    throws([&]{ (void)neo::views::unified(values, 1, -1); });
    std::forward_list<int> list{1,2,3,4,5};
    auto forward = list | neo::views::unified(2, 2);
    CHECK((std::ranges::equal(forward, std::array{1,1,3,3,5,5})));
    auto filtered = values | std::views::filter([](int v){return v % 2 == 0;})
                           | neo::views::unified(2, 2);
    CHECK((std::ranges::equal(filtered, std::array{2,2,6,6})));
    // Deterministic matrix: empty, tail cycles, and oversized steps.
    std::mt19937 rng(12345);
    for (int n = 0; n < 60; ++n) {
        std::vector<int> data(static_cast<std::size_t>(n));
        std::iota(data.begin(), data.end(), 0);
        for (int step = 1; step <= 70; step += 7) {
            for (int repeats = 1; repeats < 9; ++repeats) {
                auto r = neo::views::unified(data, step, repeats);
                std::vector<int> expected;
                for (int k = 0; k < n; k += step)
                    for (int rep = 0; rep < repeats; ++rep) expected.push_back(k);
                CHECK(r.size() == expected.size());
                CHECK(std::ranges::equal(r, expected));
                CHECK(std::ranges::distance(r) == static_cast<std::ptrdiff_t>(expected.size()));
                if (!expected.empty()) {
                    const auto index = static_cast<std::size_t>(rng() % expected.size());
                    CHECK(r.begin()[static_cast<std::ptrdiff_t>(index)] == expected[index]);
                    CHECK(*(r.end() - 1) == expected.back());
                }
            }
        }
    }
    // Input-only non-common range from an istream.
    std::istringstream input{"1 2 3 4 5"};
    auto streaming = neo::views::unified(std::ranges::istream_view<int>(input), 2, 2);
    CHECK((std::ranges::equal(streaming, std::array{1,1,3,3,5,5})));
    // owning view, with temporary vector
    auto owning = neo::views::unified(std::vector{3,4,5},2,2);
    CHECK((std::ranges::equal(owning,std::array{3,3,5,5})));
}

int main() {
    allocator_tests();
    iterator_tests();
    view_tests();
}
