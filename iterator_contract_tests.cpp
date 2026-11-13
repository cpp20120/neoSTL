#include "neo.hpp"
#include <cassert>
#include <ranges>
#include <stdexcept>

using Prefix = neo::vector<int, neo::storage::small<4>, neo::iteration::checked_prefix>;
using Conservative = neo::vector<int, neo::storage::small<4>, neo::iteration::checked>;
static_assert(std::ranges::contiguous_range<Prefix>);
static_assert(std::ranges::contiguous_range<const Prefix>);
static_assert(neo::traits::vector_traits<Prefix>::preserves_append_prefix);
static_assert(!neo::traits::vector_traits<Conservative>::preserves_append_prefix);

bool invalid(auto it) {
    try { (void)*it; return false; }
    catch (const std::logic_error&) { return true; }
}
int main() {
    Prefix v;
    v.emplace_back(10);
    auto first = v.begin();
    auto end = v.end();
    auto shifted = v.begin();
    ++shifted; // originally an element iterator, now the old end
    v.emplace_back(20);
    assert(*first == 10);
    assert(invalid(end) && invalid(shifted));
    auto second = v.begin()+1;
    v.emplace_back(30);
    assert(*first == 10 && *second == 20);
    v.emplace_back(40);
    auto old = v.begin();
    auto old_end = v.end();
    v.emplace_back(50); // forces reallocation, invalidates everything
    assert(invalid(old) && invalid(old_end));
    auto after_realloc = v.begin();
    v.pop_back(); // conservative invalidation on removal
    assert(invalid(after_realloc));

    Conservative c;
    c.emplace_back(1);
    auto iter = c.begin();
    c.emplace_back(2); // no reallocation but conservative policy invalidates
    assert(invalid(iter));

    using Fixed = neo::vector<int, neo::storage::fixed<3>, neo::iteration::checked_prefix>;
    Fixed fixed;
    fixed.emplace_back(1);
    auto keep = fixed.begin();
    fixed.emplace_back(2);
    assert(*keep == 1);
    auto before_erase = fixed.begin();
    fixed.clear();
    assert(invalid(before_erase));
}
