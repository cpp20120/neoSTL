#include "neo.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <random>
#include <vector>
#include <bit>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#define CHECK(...) do { if (!(__VA_ARGS__)) throw std::runtime_error("CHECK FAILED: " #__VA_ARGS__); } while(false)

template<class F>
void check_throws(F&& action) {
    bool threw = false;
    try { std::forward<F>(action)(); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

struct Task {
    std::string name;
    int priority;
    Task(std::string name, int priority) : name(std::move(name)), priority(priority) {}
};

struct Lifetime {
    static inline int alive = 0;
    int value;
    explicit Lifetime(int v) : value(v) { ++alive; }
    Lifetime(const Lifetime& x) : value(x.value) { ++alive; }
    Lifetime(Lifetime&& x) noexcept : value(x.value) { x.value = -1; ++alive; }
    Lifetime& operator=(Lifetime&&) = default;
    ~Lifetime() { --alive; }
};

struct Immovable {
    int value;
    explicit Immovable(int n) : value(n) {}
    Immovable(const Immovable&) = delete;
    Immovable(Immovable&&) = delete;
};

struct Fragile {
    static inline int alive = 0;
    static inline int copies = 0;
    static inline int throw_after = -1;
    int value = 0;
    explicit Fragile(int v) : value(v) { ++alive; }
    Fragile(const Fragile& x) : value(x.value) {
        if (throw_after >= 0 && ++copies >= throw_after)
            throw std::runtime_error("copy failure");
        ++alive;
    }
    Fragile(Fragile&& x) noexcept(false) : value(x.value) { x.value = -1; ++alive; }
    ~Fragile() { --alive; }
};

static_assert(neo::valid_vector_policies<int>);
static_assert(neo::valid_vector_policies<int, neo::bounds::checked, neo::storage::small<8>, neo::growth::exact>);
static_assert(!neo::valid_vector_policies<int, neo::bounds::checked, neo::bounds::unchecked>);
static_assert(!neo::valid_vector_policies<int, neo::storage::fixed<3>, neo::growth::geometric<>>);
static_assert(!neo::valid_vector_policies<int, neo::storage::external, neo::growth::exact>);
static_assert(!neo::valid_vector_policies<int, neo::storage::heap, std::false_type>);
static_assert(std::ranges::contiguous_range<neo::vector<int>>);
static_assert(std::ranges::random_access_range<neo::vector<int>>);
static_assert(std::ranges::sized_range<neo::vector<int>>);
static_assert(std::is_same_v<decltype(neo::vector{1, 2, 3}), neo::vector<int>>);
static_assert(!noexcept(std::declval<neo::vector<int,neo::bounds::checked>&>()[0]));
static_assert(noexcept(std::declval<neo::vector<int>&>()[0]));
static_assert(!std::is_copy_constructible_v<neo::vector<int,neo::storage::external>>);
template<class V>
concept can_reserve_four = requires(V& v) { v.template reserve_static<4>(); };
static_assert(can_reserve_four<neo::vector<int, neo::storage::fixed<4>>>);
static_assert(!can_reserve_four<neo::vector<int, neo::storage::fixed<3>>>);
static_assert(neo::valid_vector_policies<int, neo::growth::custom<
    [](std::size_t, std::size_t required) { return std::bit_ceil(required); }>>);
static_assert(!neo::valid_vector_policies<int, neo::storage::small<2>, neo::storage::fixed<3>>);
static_assert(!neo::valid_vector_policies<int, neo::growth::exact, neo::growth::geometric<>>);

void algorithms_demo() {
    neo::vector v{9, 3, 7, 1, 5};
    neo::sort(v);
    CHECK((std::ranges::equal(v, std::array{1,3,5,7,9})));
    CHECK(neo::lower_bound(v, 5) == v.begin() + 2);
    CHECK(neo::upper_bound(v, 5) == v.begin() + 3);

    neo::vector<Task, neo::storage::small<2>, neo::bounds::checked> tasks;
    tasks.emplace_back("alpha", 7);
    tasks.emplace_back("beta", 2);
    tasks.emplace_back("gamma", 7);   // promotes to heap
    CHECK(!tasks.using_inline_storage());
    neo::sort(tasks, neo::stable, neo::by(&Task::priority));
    CHECK(tasks[0].name == "beta");
    CHECK(tasks[1].name == "alpha" && tasks[2].name == "gamma");
    CHECK(neo::lower_bound(tasks, 7, neo::by(&Task::priority)) == tasks.begin() + 1);

    auto it = neo::nth_element<2>(v);
    CHECK(*it == 5);
    CHECK((std::ranges::all_of(v.begin(), it, [](int n){return n <= 5;})));
    CHECK((std::ranges::all_of(it + 1, v.end(), [](int n){return n >= 5;})));
    auto boundary = neo::partition(v, [](int n){return n % 2 == 0;});
    CHECK((std::ranges::all_of(v.begin(), boundary, [](int n){return n%2 == 0;})));
    neo::vector stable_partitioned{1, 2, 3, 4, 5};
    auto split = neo::partition(stable_partitioned, neo::stable,
                                [](int n) { return n % 2 == 0; });
    CHECK(split == stable_partitioned.begin() + 2);
    CHECK((std::ranges::equal(stable_partitioned, std::array{2, 4, 1, 3, 5})));

    std::array a{4, 2, 3, 1};
    neo::sort(std::span{a});
    CHECK((a == std::array{1,2,3,4}));
    neo::vector from{neo::from_range, a};
    CHECK(from.size() == 4);
    check_throws([&]{ (void)neo::nth_element(v, 100); });
}

void policy_demo() {
    // Order-independent policy pack.
    neo::vector<int, neo::bounds::checked, neo::growth::exact, neo::storage::small<2>> v;
    CHECK(v.using_inline_storage() && v.capacity() == 2);
    v.emplace_back(3);
    v.emplace_back(1);
    auto inline_address = v.data();
    v.emplace_back(2);
    CHECK(!v.using_inline_storage() && v.data() != inline_address);
    CHECK(v.capacity() == 3);
    neo::sort(v);
    CHECK((std::ranges::equal(v, std::array{1,2,3})));
    check_throws([&]{ (void)v[999]; });
    v.reserve_static<8>();
    CHECK(v.capacity() == 8);
    CHECK(v.erase_if([](int n){ return n % 2 == 0; }) == 1);
    CHECK(v.size() == 2 && v[0] == 1 && v[1] == 3);
    v.pop_back();
    CHECK(v.size() == 1);

    neo::vector<int, neo::storage::fixed<3>> fixed;
    CHECK(fixed.using_inline_storage() && fixed.capacity() == 3);
    fixed.emplace_back(4);
    fixed.emplace_back(1);
    fixed.emplace_back(2);
    auto before = fixed.data();
    neo::sort(fixed);
    CHECK((std::ranges::equal(fixed, std::array{1,2,4})));
    CHECK(fixed.data() == before);
    fixed.reserve_static<3>();
    check_throws([&]{ fixed.emplace_back(99); });
    check_throws([&]{ fixed.reserve(4); });
    CHECK(fixed.size() == 3);

    // External raw memory: caller owns the bytes; vector owns constructed objects.
    alignas(Lifetime) std::array<std::byte, sizeof(Lifetime) * 3> bytes{};
    {
        neo::vector<Lifetime, neo::storage::external> ext{std::span<std::byte>{bytes}};
        ext.emplace_back(10);
        ext.emplace_back(20);
        CHECK(Lifetime::alive == 2 && ext.size() == 2);
        check_throws([&]{ ext.reserve(4); });
        ext.pop_back();
        CHECK(Lifetime::alive == 1);
        auto moved = std::move(ext);
        CHECK(ext.empty() && moved.size() == 1);
        CHECK(moved[0].value == 10);
    }
    CHECK(Lifetime::alive == 0);

    neo::vector<Immovable, neo::storage::fixed<2>> immovable;
    immovable.emplace_back(1);
    immovable.emplace_back(2);
    CHECK(immovable[1].value == 2);
    check_throws([&]{ immovable.emplace_back(3); });

    neo::vector<int, neo::storage::fixed<0>> empty_fixed;
    CHECK(empty_fixed.capacity() == 0);
    check_throws([&]{ empty_fixed.emplace_back(1); });
    neo::vector<int, neo::storage::small<0>> zero_small;
    zero_small.emplace_back(7);
    CHECK(zero_small.size() == 1 && !zero_small.using_inline_storage());

    neo::vector<int, neo::growth::custom<
        [](std::size_t, std::size_t required) { return std::bit_ceil(required); }>> powers_of_two;
    for (int i = 0; i < 17; ++i) powers_of_two.emplace_back(i);
    CHECK(powers_of_two.capacity() == 32);

    // Move from inline -> each element moves; move from heap -> pointer steals.
    neo::vector<Lifetime, neo::storage::small<2>> l;
    l.emplace_back(1);
    auto inline_move = std::move(l);
    CHECK(inline_move.size() == 1 && l.empty() && inline_move.using_inline_storage());
    CHECK(Lifetime::alive == 1);
    inline_move.emplace_back(2);
    inline_move.emplace_back(3);
    auto heap_ptr = inline_move.data();
    auto stolen = std::move(inline_move);
    CHECK(stolen.data() == heap_ptr && inline_move.empty());
    CHECK(Lifetime::alive == 3);
}

void randomized_consistency() {
    std::mt19937 gen(0xA11CE);
    neo::vector<int, neo::storage::small<8>> actual;
    std::vector<int> reference;
    for (int i = 0; i < 3000; ++i) {
        const int operation = static_cast<int>(gen() % 6);
        if (operation <= 1) {
            int value = static_cast<int>(gen() % 1000);
            actual.emplace_back(value);
            reference.emplace_back(value);
        } else if (operation == 2 && !reference.empty()) {
            actual.pop_back();
            reference.pop_back();
        } else if (operation == 3) {
            auto n = static_cast<std::size_t>(gen() % 128);
            actual.reserve(n);
            reference.reserve(n);
        } else if (operation == 4) {
            const auto erased = actual.erase_if([](int x) { return x % 13 == 0; });
            const auto expected = std::erase_if(reference, [](int x) { return x % 13 == 0; });
            CHECK(erased == expected);
        } else {
            neo::sort(actual);
            std::ranges::sort(reference);
        }
        CHECK(actual.size() == reference.size());
        CHECK(std::ranges::equal(actual, reference));
        if (i % 31 == 0) {
            auto copy = actual;
            CHECK(std::ranges::equal(copy, reference));
            neo::vector<int, neo::storage::small<8>> assigned;
            assigned = actual;
            CHECK(std::ranges::equal(assigned, reference));
            auto moved = std::move(copy);
            CHECK(std::ranges::equal(moved, reference));
        }
    }
}

void exception_and_alias_demo() {
    {
        neo::vector<std::string, neo::storage::small<1>> words;
        words.emplace_back("payload");
        words.emplace_back(words[0]); // alias-safe insertion when growth is required
        CHECK(words[0] == "payload" && words[1] == "payload");
    }
    {
        neo::vector<Fragile, neo::storage::small<1>> v;
        v.emplace_back(7);
        Fragile::copies = 0;
        Fragile::throw_after = 1;
        check_throws([&]{ v.reserve(20); });
        Fragile::throw_after = -1;
        CHECK(v.size() == 1 && v[0].value == 7);
        CHECK(Fragile::alive == 1);
    }
    CHECK(Fragile::alive == 0);
    {
        std::array<Fragile, 3> originals{Fragile(1), Fragile(2), Fragile(3)};
        const auto before = Fragile::alive;
        Fragile::copies = 0;
        Fragile::throw_after = 2;
        check_throws([&] {
            neo::vector<Fragile, neo::storage::small<4>> partial{neo::from_range, originals};
        });
        Fragile::throw_after = -1;
        CHECK(Fragile::alive == before); // no leaked partially constructed objects
    }
    CHECK(Fragile::alive == 0);
}

int main() {
    algorithms_demo();
    policy_demo();
    exception_and_alias_demo();
    randomized_consistency();
    CHECK(Lifetime::alive == 0);
    std::cout << "neo STL v2: all tests passed\n";
}
