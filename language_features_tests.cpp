#include "neo.hpp"

#include <array>
#include <cassert>
#include <concepts>
#include <list>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

using Fixed = neo::vector<int, neo::storage::fixed<8>>;

// This has an actual T array during constant evaluation, not an array of
// union-slots that would invalidate T* arithmetic.
static_assert([] {
    Fixed v;
    v.emplace_back(7);
    v.emplace_back(4);
    if (v.size() != 2 || v.back() != 4) return false;
    v.pop_back();
    v.emplace_back(9);
    return v[0] == 7 && v.span()[1] == 9 && v.end() - v.begin() == 2;
}());

static_assert([] {
    neo::vector<int, neo::storage::small<4>> v;
    v.emplace_back(3);
    v.emplace_back(6);
    v.pop_back();
    v.emplace_back(12);
    return v.using_inline_storage() && v.size() == 2 && v.span()[1] == 12;
}());

static_assert(neo::growth::exact::operator()(10, 13) == 13);
static_assert(neo::growth::geometric<2, 1>::operator()(4, 6) == 8);
static_assert(std::is_same_v<decltype(std::declval<Fixed&>().data()), int*>);
static_assert(std::is_same_v<decltype(std::declval<const Fixed&>().data()), const int*>);
static_assert(std::is_same_v<decltype(std::declval<Fixed&>().front()), int&>);
static_assert(std::is_same_v<decltype(std::declval<const Fixed&>().front()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<Fixed&>().span()), std::span<int>>);
static_assert(std::is_same_v<decltype(std::declval<const Fixed&>().span()), std::span<const int>>);

int main() {
    Fixed v;
    v.emplace_back(5);
    v.emplace_back(8);
    auto& cv = std::as_const(v);
    assert(cv.at(0) == 5 && cv.back() == 8);
    assert(*cv.rbegin() == 8);

    std::array values{1, 2, 3, 4, 5};
    auto view = neo::views::unified_fn::operator()(values, 2, 2);
    assert(std::ranges::distance(view) == 6);
    assert(*view.begin() == 1);
    const auto& const_view = view;
    assert(std::ranges::distance(const_view) == 6);
}
