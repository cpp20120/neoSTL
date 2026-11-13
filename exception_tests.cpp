#include "neo.hpp"
#include <array>
#include <cassert>
#include <stdexcept>
#include <type_traits>

struct CopyBomb {
    int id = 0;
    static inline int copies_left = 1000;
    explicit CopyBomb(int x) : id(x) {}
    CopyBomb(const CopyBomb& x) : id(x.id) {
        if (--copies_left < 0) throw std::runtime_error("copy bomb");
    }
    CopyBomb(CopyBomb&& x) noexcept(false) : id(x.id) { x.id = -1; }
    CopyBomb& operator=(const CopyBomb&) = default;
};
struct MoveBomb {
    MoveBomb() = default;
    MoveBomb(const MoveBomb&) = delete;
    MoveBomb(MoveBomb&&) noexcept(false) { throw 1; }
};
using Strong = neo::vector<CopyBomb, neo::growth::exact, neo::exception_safety::strong_growth>;
static_assert(neo::valid_vector_policies<CopyBomb, neo::exception_safety::strong_growth>);
static_assert(!neo::valid_vector_policies<MoveBomb, neo::exception_safety::strong_growth>);
static_assert(neo::valid_vector_policies<MoveBomb, neo::exception_safety::basic>);
static_assert(neo::valid_vector_policies<MoveBomb, neo::storage::fixed<5>, neo::exception_safety::strong_growth>);
static_assert(!neo::valid_vector_policies<CopyBomb, neo::relocation::always_move, neo::exception_safety::strong_growth>);
static_assert(neo::valid_vector_policies<CopyBomb, neo::relocation::copy, neo::exception_safety::strong_growth>);
static_assert(neo::traits::vector_traits<Strong>::strong_growth_guarantee);

void check_unchanged(Strong& v, const CopyBomb* before, std::size_t capacity) {
    assert(v.size() == 2);
    assert(v.capacity() == capacity);
    assert(v.data() == before);
    assert(v[0].id == 10 && v[1].id == 20);
}
int main() {
    Strong v;
    v.emplace_back(10);
    v.emplace_back(20);
    auto* before = v.data();
    auto cap = v.capacity();
    // Append-element copy throws before relocating previous elements.
    CopyBomb::copies_left = 0;
    try { v.emplace_back(30); assert(false); } catch (const std::runtime_error&) {}
    check_unchanged(v, before, cap);
    // Existing-element copy throws after the newly appended element was built.
    CopyBomb::copies_left = 1;
    try { v.emplace_back(30); assert(false); } catch (const std::runtime_error&) {}
    check_unchanged(v, before, cap);
    // reserve() has the same rollback contract.
    CopyBomb::copies_left = 1;
    try { v.reserve(100); assert(false); } catch (const std::runtime_error&) {}
    check_unchanged(v, before, cap);
    CopyBomb::copies_left = 100;
    v.emplace_back(30);
    assert(v[2].id == 30);
    using Small = neo::vector<CopyBomb, neo::storage::small<2>, neo::exception_safety::strong_growth>;
    Small s;
    s.emplace_back(1); s.emplace_back(2);
    auto* inline_data = s.data();
    CopyBomb::copies_left = 1;
    try { s.emplace_back(3); assert(false); } catch (const std::runtime_error&) {}
    assert(s.size() == 2 && s.data() == inline_data && s.using_inline_storage());
}
