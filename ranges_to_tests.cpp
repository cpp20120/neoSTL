#include "neo.hpp"

#include <array>
#include <cassert>
#include <concepts>
#include <list>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <sstream>
#include <type_traits>
#include <vector>

int main() {
    std::array source{1, 2, 3, 4};

    neo::vector from{neo::from_range, source};
    static_assert(std::same_as<decltype(from), neo::vector<int>>);
    assert(from[2] == 3);

    auto direct = std::ranges::to<neo::vector<int>>(source);
    assert(direct.size() == 4 && direct.back() == 4);

    // std::ranges::to works with the Neo template itself, including CTAD.
    auto deduced = std::ranges::to<neo::vector>(source);
    static_assert(std::same_as<decltype(deduced), neo::vector<int>>);
    assert(deduced[1] == 2);

    auto piped = source
        | std::views::transform([](int x) { return x * 3; })
        | std::ranges::to<neo::vector<int>>();
    assert(piped[2] == 9);

    auto piped_deduced = source | std::ranges::to<neo::vector>();
    assert(piped_deduced[3] == 4);

    // Non-common input-only range and a move-only destination element.
    std::istringstream input{"10 20 30"};
    auto stream = std::ranges::istream_view<int>(input);
    auto single_pass = std::ranges::to<neo::vector<int>>(stream);
    assert(single_pass.size() == 3 && single_pass[1] == 20);

    // Nested range conversion: ranges::to recursively materializes elements.
    std::array<std::array<int, 2>, 2> matrix{{{1, 2}, {3, 4}}};
    auto nested = std::ranges::to<neo::vector<neo::vector<int>>>(matrix);
    assert(nested[1][0] == 3);

    // Explicit allocator-aware target can still use ranges::to's args.
    std::byte arena[4096];
    std::pmr::monotonic_buffer_resource resource{arena, sizeof(arena)};
    auto pmr = std::ranges::to<neo::vector<int, neo::allocation::pmr>>(source, &resource);
    assert(pmr.get_allocator().resource() == &resource && pmr[3] == 4);

    neo::vector<int, neo::allocation::pmr> with_standard_order{
        neo::from_range, source, std::pmr::polymorphic_allocator<int>{&resource}};
    assert(with_standard_order.get_allocator().resource() == &resource);
    with_standard_order.assign_range(source | std::views::transform([](int x) { return x + 10; }));
    assert(with_standard_order.size() == 4 && with_standard_order[0] == 11);

    // Integral user-defined policy is preserved when the destination is explicit.
    auto fixed = std::ranges::to<neo::vector<int, neo::storage::fixed<8>>>(source);
    assert(fixed.capacity() == 8 && fixed.size() == 4);

    // Direct iterator/sentinel construction and CTAD.
    neo::vector from_iterators(source.begin(), source.end());
    static_assert(std::same_as<decltype(from_iterators), neo::vector<int>>);
    assert(from_iterators.size() == source.size());

    // A Neo vector is itself a range that standard containers can consume.
    auto back_to_std = std::ranges::to<std::vector<int>>(direct);
    assert((back_to_std == std::vector<int>{1, 2, 3, 4}));

    auto move_only = source
        | std::views::transform([](int x) { return std::make_unique<int>(x); })
        | std::ranges::to<neo::vector<std::unique_ptr<int>>>();
    assert(move_only.size() == 4 && *move_only[3] == 4);

#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
    neo::vector standard_tag{std::from_range, source};
    assert(standard_tag.size() == 4);
#endif

    // No new types or non-standard ranges::to overloads needed.
    auto unified = source | neo::views::unified(2, 3) | std::ranges::to<neo::vector<int>>();
    assert(unified.size() == 6 && unified[3] == 3);
}
