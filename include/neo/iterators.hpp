#pragma once
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace neo::detail {
template<class T>
struct iterator_state {
    T* data = nullptr;
    std::size_t size = 0;
    std::uint64_t generation = 0;
    std::uint64_t append_generation = 0;
};
struct no_iterator_state {};

template<class T, bool Constant = false, bool PreserveAppendPrefix = false>
class checked_iterator {
    template<class, bool, bool> friend class checked_iterator;
    using state_type = iterator_state<T>;
    state_type* state_ = nullptr;
    std::ptrdiff_t index_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t append_generation_ = 0;
    bool past_end_at_creation_ = false;

    void valid() const {
        if (!state_ || state_->generation != generation_)
            throw std::logic_error("neo::checked_iterator invalidated");
        if constexpr (PreserveAppendPrefix) {
            if (past_end_at_creation_ && state_->append_generation != append_generation_)
                throw std::logic_error("neo::checked_iterator old end invalidated by append");
        }
    }
    void comparable(const checked_iterator& other) const {
        if (!state_ && !other.state_) return;
        valid(); other.valid();
        if (state_ != other.state_)
            throw std::logic_error("neo::checked_iterator different containers");
    }
    void in_range(std::ptrdiff_t index) const {
        valid();
        if (index < 0 || static_cast<std::size_t>(index) > state_->size)
            throw std::out_of_range("neo::checked_iterator outside range");
    }
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using reference = std::conditional_t<Constant, const T&, T&>;
    using pointer = std::conditional_t<Constant, const T*, T*>;
    using iterator_concept = std::contiguous_iterator_tag;
    using iterator_category = std::random_access_iterator_tag;

    checked_iterator() = default;
    checked_iterator(state_type* state, difference_type position) noexcept
        : state_(state), index_(position), generation_(state ? state->generation : 0),
          append_generation_(state ? state->append_generation : 0),
          past_end_at_creation_(state && position == static_cast<difference_type>(state->size)) {}

    template<bool C = Constant> requires C
    checked_iterator(const checked_iterator<T, false, PreserveAppendPrefix>& other) noexcept
        : state_(other.state_), index_(other.index_), generation_(other.generation_),
          append_generation_(other.append_generation_),
          past_end_at_creation_(other.past_end_at_creation_) {}

    reference operator*() const {
        valid();
        if (index_ < 0 || static_cast<std::size_t>(index_) >= state_->size)
            throw std::out_of_range("neo::checked_iterator dereference end");
        return state_->data[index_];
    }
    pointer operator->() const { return std::addressof(operator*()); }
    reference operator[](difference_type offset) const { return *(*this + offset); }

    checked_iterator& operator++() { return *this += 1; }
    checked_iterator operator++(int) { auto old = *this; ++*this; return old; }
    checked_iterator& operator--() { return *this -= 1; }
    checked_iterator operator--(int) { auto old = *this; --*this; return old; }
    checked_iterator& operator+=(difference_type n) {
        valid();
        // No overflow: both current index and size are nonnegative and fit ptrdiff_t.
        if (n < -index_ || n > static_cast<difference_type>(state_->size) - index_)
            throw std::out_of_range("neo::checked_iterator advance outside range");
        index_ += n;
        if constexpr (PreserveAppendPrefix) {
            // A position that has become end() must not silently become a
            // newly appended element. Record the most recent append epoch.
            past_end_at_creation_ = (index_ == static_cast<difference_type>(state_->size));
            append_generation_ = state_->append_generation;
        }
        return *this;
    }
    checked_iterator& operator-=(difference_type n) {
        // Avoid negating PTRDIFF_MIN.
        if (n == std::numeric_limits<difference_type>::min())
            throw std::out_of_range("neo::checked_iterator subtract overflow");
        return *this += -n;
    }
    friend checked_iterator operator+(checked_iterator it, difference_type n) { it += n; return it; }
    friend checked_iterator operator+(difference_type n, checked_iterator it) { it += n; return it; }
    friend checked_iterator operator-(checked_iterator it, difference_type n) { it -= n; return it; }
    friend difference_type operator-(const checked_iterator& a, const checked_iterator& b) {
        a.comparable(b); return a.index_ - b.index_;
    }
    friend bool operator==(const checked_iterator& a, const checked_iterator& b) {
        a.comparable(b); return a.index_ == b.index_;
    }
    friend std::strong_ordering operator<=>(const checked_iterator& a, const checked_iterator& b) {
        a.comparable(b); return a.index_ <=> b.index_;
    }
};
}
