#include "neo.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <forward_list>
#include <list>
#include <ranges>
#include <type_traits>
#include <vector>

using namespace neo;

// Demonstrate the public extension point for independently authored adaptors.
struct negate_view_fn : neo::views::adaptor_closure<negate_view_fn> {
    template<std::ranges::viewable_range R>
    constexpr auto operator()(R&& range) const {
        return std::forward<R>(range) |
            std::views::transform([](int x) { return -x; });
    }
};
inline constexpr negate_view_fn negate_view{};

constexpr auto one = views::unified(2, 2);
constexpr auto two = views::unified_c<3, 2>;
static_assert(views::adaptor_for<decltype(one), std::array<int, 7>&>);
static_assert(views::adaptor_for<decltype(two), std::array<int, 7>&>);
static_assert(!views::adaptor_for<decltype(one), int>);

// neo | neo: both runtime-state and NTTP adaptor closures.
constexpr auto pair = one | two;
constexpr auto custom = views::unified(2, 2) | negate_view;
static_assert(views::adaptor_for<decltype(custom), std::array<int, 7>&>);
static_assert(views::adaptor_for<decltype(pair), std::array<int, 7>&>);

#if defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 202202L
// Standard C++23 P2387R3 adaptor | adaptor composition, in both directions.
auto std_after_neo = views::unified(2, 2) |
    std::views::transform([](int n) { return n * 10; });
auto neo_after_std = std::views::filter([](int n) { return n % 2 != 0; }) |
    views::unified(2, 2);
auto mixed = std::views::filter([](int n) { return n % 2 != 0; }) |
    views::unified_c<2, 2> |
    std::views::transform([](int n) { return n + 1; });
static_assert(views::adaptor_for<decltype(std_after_neo)&, std::array<int, 7>&>);
static_assert(views::adaptor_for<decltype(neo_after_std)&, std::array<int, 7>&>);
#endif

constexpr bool constexpr_pipeline() {
    std::array values{1,2,3,4,5};
    auto pipeline = views::unified_c<2,2> | views::unified_c<2,2>;
    auto result = values | pipeline;
    return std::ranges::equal(result, std::array{1,1,3,3,5,5});
}
static_assert(constexpr_pipeline());

int main() {
    std::array values{1,2,3,4,5,6,7};
    auto result = values | pair;
    assert((std::ranges::equal(values | custom,
        std::array{-1,-1,-3,-3,-5,-5,-7,-7})));
    assert((std::ranges::equal(result,
        std::array{1,1,3,3,7,7})));
#if defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 202202L
    assert((std::ranges::equal(values | std_after_neo,
        std::array{10,10,30,30,50,50,70,70})));
    assert((std::ranges::equal(values | neo_after_std,
        std::array{1,1,5,5})));
    assert((std::ranges::equal(values | mixed,
        std::array{2,2,6,6})));
#endif
    std::list<int> list{1,2,3,4,5};
    auto bidir = list | (views::unified(2,3) | views::unified_c<2,1>);
    static_assert(std::ranges::bidirectional_range<decltype(bidir)>);
    assert((std::ranges::equal(bidir, std::array{1,1,3,5,5})));

    std::forward_list<int> forward{1,2,3,4,5};
    auto fw = forward | (views::unified(2,2) | views::unified_c<2,1>);
    static_assert(std::ranges::forward_range<decltype(fw)>);
    assert((std::ranges::equal(fw, std::array{1,3,5})));
}
