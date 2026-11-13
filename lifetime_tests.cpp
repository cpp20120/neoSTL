#include "neo.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

struct alignas(64) Record {
    static inline int alive = 0;
    const int id;
    std::string payload;
    Record(int id, std::string payload) : id(id), payload(std::move(payload)) { ++alive; }
    Record(const Record& r) : id(r.id), payload(r.payload) { ++alive; }
    Record(Record&& r) noexcept(false) : id(r.id), payload(std::move(r.payload)) { ++alive; }
    ~Record() { --alive; }
    Record& operator=(const Record&) = delete;
    Record& operator=(Record&&) = delete;
};

static_assert(!std::default_initializable<Record>);
static_assert(alignof(Record) > alignof(std::max_align_t));

template<class V>
void check_lifecycle(V& v) {
    assert(v.empty());
    // A raw address exists, but no Record exists there yet. Do not launder it.
    (void)v.data();
    auto& a = v.emplace_back(11, "old");
    assert(a.id == 11 && a.payload == "old");
    assert(reinterpret_cast<std::uintptr_t>(v.data()) % alignof(Record) == 0);
    assert(v.data()[0].id == 11);
    assert(v.span()[0].payload == "old");
    v.emplace_back(12, "next");
    assert(std::ranges::distance(v) == 2);
    assert(v.span()[1].id == 12);
    v.pop_back();
    v.pop_back();
    assert(v.empty());
    v.emplace_back(13, "reused");
    assert(v[0].id == 13 && v[0].payload == "reused");
    v.clear();
    assert(Record::alive == 0);
}

int main() {
    {
        neo::vector<Record, neo::storage::small<2>> v;
        check_lifecycle(v);
        v.emplace_back(42, "one");
        v.emplace_back(43, "two");
        v.emplace_back(44, "three"); // relocate off inline storage
        assert(!v.using_inline_storage() && v.size() == 3);
        assert(v[0].id == 42 && v[1].id == 43 && v[2].id == 44);
        v.clear();
    }
    assert(Record::alive == 0);
    {
        neo::vector<Record, neo::storage::fixed<2>> v;
        check_lifecycle(v);
    }
    assert(Record::alive == 0);
    {
        alignas(Record) std::array<std::byte, sizeof(Record) * 2> raw{};
        neo::vector<Record, neo::storage::external> v{std::span<std::byte>{raw}};
        check_lifecycle(v);
    }
    assert(Record::alive == 0);
    {
        neo::vector<int, neo::storage::small<0>> v;
        assert(v.size() == 0 && v.capacity() == 0);
        v.emplace_back(4); // zero-capacity case must allocate, not launder
        assert(v[0] == 4 && !v.using_inline_storage());
        neo::vector<int, neo::storage::fixed<0>> f;
        assert(f.size() == 0 && f.capacity() == 0);
    }
    {
        neo::vector<int, neo::storage::small<4>> v{4, 1, 3, 2};
        neo::sort(v);
        assert((std::ranges::equal(v, std::array{1, 2, 3, 4})));
        v.clear();
        v.emplace_back(55);
        assert(v.front() == 55);
    }
    assert(Record::alive == 0);
}
