#pragma once

#include "range_adaptor.hpp"

#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace neo {

// Inspired by cpp20120/MetaUtils unified_view.hpp:
// visit indices 0, step, 2*step, ... and repeat each selected element `count` times.
// This view intentionally exposes read-only elements: repeated references alias and
// a mutating algorithm such as sort would otherwise violate its assumptions.
template<std::ranges::input_range V>
    requires std::ranges::view<V>
class unified_view : public std::ranges::view_interface<unified_view<V>> {
    V base_;
    std::ptrdiff_t step_ = 1;
    std::ptrdiff_t count_ = 1;

    static constexpr std::size_t count_elements(
        std::size_t n, std::size_t step, std::size_t count) {
        const auto selected = n / step + static_cast<std::size_t>(n % step != 0);
        constexpr auto limit = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
        if (selected > limit / count)
            throw std::length_error("neo::unified_view size exceeds ptrdiff_t");
        return selected * count;
    }

    template<class Iter>
    static constexpr decltype(auto) read_only(Iter const& it) {
        if constexpr (std::is_lvalue_reference_v<decltype(*it)>)
            return std::as_const(*it);
        else
            return *it;
    }

    template<bool Const>
    using range_t = std::conditional_t<Const, const V, V>;

    template<bool Const>
    class indexed_iterator {
        using R = range_t<Const>;
        using base_iterator = std::ranges::iterator_t<R>;
        base_iterator begin_{};
        std::ptrdiff_t index_ = 0;
        std::ptrdiff_t step_ = 1;
        std::ptrdiff_t count_ = 1;
    public:
        using value_type = std::ranges::range_value_t<R>;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::random_access_iterator_tag;
        using iterator_category = std::random_access_iterator_tag;
        using reference = decltype(read_only(std::declval<base_iterator const&>()));

        indexed_iterator() = default;
        constexpr indexed_iterator(base_iterator first, difference_type index,
                                   difference_type step, difference_type count)
            : begin_(std::move(first)), index_(index), step_(step), count_(count) {}
        constexpr decltype(auto) operator*() const {
            return read_only(begin_ + ((index_ / count_) * step_));
        }
        constexpr auto operator->() const requires std::is_lvalue_reference_v<reference> {
            return std::addressof(operator*());
        }
        constexpr decltype(auto) operator[](difference_type n) const { return *(*this + n); }
        constexpr indexed_iterator& operator++() noexcept { ++index_; return *this; }
        constexpr indexed_iterator operator++(int) { auto copy = *this; ++*this; return copy; }
        constexpr indexed_iterator& operator--() noexcept { --index_; return *this; }
        constexpr indexed_iterator operator--(int) { auto copy = *this; --*this; return copy; }
        constexpr indexed_iterator& operator+=(difference_type n) noexcept { index_ += n; return *this; }
        constexpr indexed_iterator& operator-=(difference_type n) noexcept { index_ -= n; return *this; }
        friend constexpr indexed_iterator operator+(indexed_iterator it, difference_type n) { it += n; return it; }
        friend constexpr indexed_iterator operator+(difference_type n, indexed_iterator it) { it += n; return it; }
        friend constexpr indexed_iterator operator-(indexed_iterator it, difference_type n) { it -= n; return it; }
        friend constexpr difference_type operator-(const indexed_iterator& a, const indexed_iterator& b) noexcept {
            return a.index_ - b.index_;
        }
        friend constexpr bool operator==(const indexed_iterator& a, const indexed_iterator& b) noexcept {
            return a.index_ == b.index_;
        }
        friend constexpr auto operator<=>(const indexed_iterator& a, const indexed_iterator& b) noexcept {
            return a.index_ <=> b.index_;
        }
    };

    template<bool Const>
    class linear_iterator {
        using R = range_t<Const>;
        using base_iterator = std::ranges::iterator_t<R>;
        using base_sentinel = std::ranges::sentinel_t<R>;
        base_iterator current_{};
        base_sentinel last_{};
        std::ptrdiff_t step_ = 1;
        std::ptrdiff_t count_ = 1;
        std::ptrdiff_t repeated_ = 0;
    public:
        using value_type = std::ranges::range_value_t<R>;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::conditional_t<std::ranges::forward_range<R>,
            std::forward_iterator_tag, std::input_iterator_tag>;
        using iterator_category = iterator_concept;
        using reference = decltype(read_only(std::declval<base_iterator const&>()));

        linear_iterator() = default;
        constexpr linear_iterator(base_iterator first, base_sentinel last,
                                  difference_type step, difference_type count)
            : current_(std::move(first)), last_(std::move(last)), step_(step), count_(count) {}
        constexpr decltype(auto) operator*() const { return read_only(current_); }
        constexpr linear_iterator& operator++() {
            if (++repeated_ == count_) {
                repeated_ = 0;
                for (difference_type n = 0; n < step_ && current_ != last_; ++n)
                    ++current_;
            }
            return *this;
        }
        constexpr void operator++(int) requires (!std::ranges::forward_range<R>) { ++*this; }
        constexpr linear_iterator operator++(int) requires std::ranges::forward_range<R> {
            auto copy = *this; ++*this; return copy;
        }
        friend constexpr bool operator==(const linear_iterator& it, std::default_sentinel_t) {
            return it.current_ == it.last_;
        }
        friend constexpr bool operator==(const linear_iterator& a, const linear_iterator& b)
            requires std::ranges::forward_range<R> {
            return a.current_ == b.current_ && a.repeated_ == b.repeated_;
        }
    };

    // A bidirectional, common base supports decrementing from the logical end.
    // For unsized bases --end() may scan the base once to find the last selected
    // element; ++/-- at ordinary positions take O(step) increments/decrements.
    template<bool Const>
    class bidirectional_iterator {
        using R = range_t<Const>;
        using base_iterator = std::ranges::iterator_t<R>;
        base_iterator begin_{};
        base_iterator current_{};
        base_iterator last_{};
        std::ptrdiff_t step_ = 1;
        std::ptrdiff_t count_ = 1;
        std::ptrdiff_t repeated_ = 0;
    public:
        using value_type = std::ranges::range_value_t<R>;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::bidirectional_iterator_tag;
        using iterator_category = std::bidirectional_iterator_tag;
        using reference = decltype(read_only(std::declval<base_iterator const&>()));

        bidirectional_iterator() = default;
        constexpr bidirectional_iterator(base_iterator begin, base_iterator current,
                                         base_iterator last, difference_type step,
                                         difference_type count)
            : begin_(std::move(begin)), current_(std::move(current)),
              last_(std::move(last)), step_(step), count_(count) {}
        constexpr decltype(auto) operator*() const { return read_only(current_); }
        constexpr bidirectional_iterator& operator++() {
            if (++repeated_ == count_) {
                repeated_ = 0;
                for (difference_type n = 0; n < step_ && current_ != last_; ++n)
                    ++current_;
            }
            return *this;
        }
        constexpr bidirectional_iterator operator++(int) {
            auto copy = *this; ++*this; return copy;
        }
        constexpr bidirectional_iterator& operator--() {
            if (current_ == last_) {
                const auto n = std::ranges::distance(begin_, last_);
                // --end() requires a nonempty view, as for ordinary iterators.
                const auto back = (n - 1) % step_ + 1;
                std::ranges::advance(current_, -back);
                repeated_ = count_ - 1;
            } else if (repeated_ > 0) {
                --repeated_;
            } else {
                std::ranges::advance(current_, -step_);
                repeated_ = count_ - 1;
            }
            return *this;
        }
        constexpr bidirectional_iterator operator--(int) {
            auto copy = *this; --*this; return copy;
        }
        friend constexpr bool operator==(const bidirectional_iterator& lhs,
                                          const bidirectional_iterator& rhs) {
            return lhs.current_ == rhs.current_ && lhs.repeated_ == rhs.repeated_;
        }
    };

    template<class R>
    static constexpr bool indexed = std::ranges::random_access_range<R> && std::ranges::sized_range<R>;

    // Deliberately distinct constrained overloads: these are separate iterator
    // contracts, not a single implementation pretending every range is forward.
    template<bool Const, class Self>
        requires indexed<range_t<Const>>
    static constexpr auto begin_impl(Self& self) {
        return indexed_iterator<Const>{std::ranges::begin(self.base_),
                                       0, self.step_, self.count_};
    }
    template<bool Const, class Self>
        requires (!indexed<range_t<Const>> &&
                  std::ranges::bidirectional_range<range_t<Const>> &&
                  std::ranges::common_range<range_t<Const>>)
    static constexpr auto begin_impl(Self& self) {
        auto first = std::ranges::begin(self.base_);
        return bidirectional_iterator<Const>{first, first, std::ranges::end(self.base_),
                                             self.step_, self.count_};
    }
    template<bool Const, class Self>
        requires (!indexed<range_t<Const>> &&
                  std::ranges::forward_range<range_t<Const>> &&
                  !std::ranges::bidirectional_range<range_t<Const>>)
    static constexpr auto begin_impl(Self& self) {
        return linear_iterator<Const>{std::ranges::begin(self.base_),
                                      std::ranges::end(self.base_),
                                      self.step_, self.count_};
    }
    // A non-common bidirectional base cannot expose a common --end() here.
    // Conservatively expose a forward iterator with a sentinel instead.
    template<bool Const, class Self>
        requires (!indexed<range_t<Const>> &&
                  std::ranges::bidirectional_range<range_t<Const>> &&
                  !std::ranges::common_range<range_t<Const>>)
    static constexpr auto begin_impl(Self& self) {
        return linear_iterator<Const>{std::ranges::begin(self.base_),
                                      std::ranges::end(self.base_),
                                      self.step_, self.count_};
    }
    template<bool Const, class Self>
        requires (!std::ranges::forward_range<range_t<Const>>)
    static constexpr auto begin_impl(Self& self) {
        return linear_iterator<Const>{std::ranges::begin(self.base_),
                                      std::ranges::end(self.base_),
                                      self.step_, self.count_};
    }

    template<bool Const, class Self>
        requires indexed<range_t<Const>>
    static constexpr auto end_impl(Self& self) {
        const auto length = count_elements(std::ranges::size(self.base_),
            static_cast<std::size_t>(self.step_), static_cast<std::size_t>(self.count_));
        return indexed_iterator<Const>{std::ranges::begin(self.base_),
            static_cast<std::ptrdiff_t>(length), self.step_, self.count_};
    }
    template<bool Const, class Self>
        requires (!indexed<range_t<Const>> &&
                  std::ranges::bidirectional_range<range_t<Const>> &&
                  std::ranges::common_range<range_t<Const>>)
    static constexpr auto end_impl(Self& self) {
        return bidirectional_iterator<Const>{std::ranges::begin(self.base_),
            std::ranges::end(self.base_), std::ranges::end(self.base_),
            self.step_, self.count_};
    }
    template<bool Const, class Self>
        requires (!indexed<range_t<Const>> &&
                  std::ranges::forward_range<range_t<Const>> &&
                  !std::ranges::bidirectional_range<range_t<Const>>)
    static constexpr auto end_impl(Self&) noexcept { return std::default_sentinel; }
    template<bool Const, class Self>
        requires (!indexed<range_t<Const>> &&
                  std::ranges::bidirectional_range<range_t<Const>> &&
                  !std::ranges::common_range<range_t<Const>>)
    static constexpr auto end_impl(Self&) noexcept { return std::default_sentinel; }
    template<bool Const, class Self>
        requires (!std::ranges::forward_range<range_t<Const>>)
    static constexpr auto end_impl(Self&) noexcept { return std::default_sentinel; }

public:
    unified_view() requires std::default_initializable<V> = default;
    constexpr unified_view(V base, std::ptrdiff_t step, std::ptrdiff_t count)
        : base_(std::move(base)), step_(step), count_(count) {
        if (step <= 0 || count <= 0)
            throw std::invalid_argument("neo::unified_view requires positive step/count");
    }
#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
    template<class Self>
    constexpr auto begin(this Self& self)
        requires std::ranges::input_range<
            std::conditional_t<std::is_const_v<Self>, const V, V>> {
        return begin_impl<std::is_const_v<Self>>(self);
    }
    template<class Self>
    constexpr auto end(this Self& self)
        requires std::ranges::input_range<
            std::conditional_t<std::is_const_v<Self>, const V, V>> {
        return end_impl<std::is_const_v<Self>>(self);
    }
    template<class Self>
    constexpr std::size_t size(this Self& self)
        requires std::ranges::sized_range<
            std::conditional_t<std::is_const_v<Self>, const V, V>> {
        return count_elements(std::ranges::size(self.base_),
            static_cast<std::size_t>(self.step_), static_cast<std::size_t>(self.count_));
    }
#else
    constexpr auto begin() { return begin_impl<false>(*this); }
    constexpr auto end() { return end_impl<false>(*this); }
    constexpr auto begin() const requires std::ranges::input_range<const V> {
        return begin_impl<true>(*this);
    }
    constexpr auto end() const requires std::ranges::input_range<const V> {
        return end_impl<true>(*this);
    }
    constexpr std::size_t size() requires std::ranges::sized_range<V> {
        return count_elements(std::ranges::size(base_),
            static_cast<std::size_t>(step_), static_cast<std::size_t>(count_));
    }
    constexpr std::size_t size() const requires std::ranges::sized_range<const V> {
        return count_elements(std::ranges::size(base_),
            static_cast<std::size_t>(step_), static_cast<std::size_t>(count_));
    }
#endif
    constexpr V base() const& noexcept(std::is_nothrow_copy_constructible_v<V>)
        requires std::copy_constructible<V> { return base_; }
    constexpr V base() && noexcept(std::is_nothrow_move_constructible_v<V>) { return std::move(base_); }
};

template<std::ranges::viewable_range R>
unified_view(R&&, std::ptrdiff_t, std::ptrdiff_t) -> unified_view<std::views::all_t<R>>;

namespace views {
struct unified_closure : adaptor_closure<unified_closure> {
    std::ptrdiff_t step, count;
    constexpr unified_closure(std::ptrdiff_t s, std::ptrdiff_t c) : step(s), count(c) {}
    template<std::ranges::viewable_range R>
    constexpr auto operator()(R&& range) const {
        return unified_view{std::views::all(std::forward<R>(range)), step, count};
    }
};
struct unified_fn {
    template<std::ranges::viewable_range R>
    static constexpr auto operator()(R&& range, std::ptrdiff_t step, std::ptrdiff_t count) {
        return unified_view{std::views::all(std::forward<R>(range)), step, count};
    }
    static constexpr auto operator()(std::ptrdiff_t step, std::ptrdiff_t count) {
        return unified_closure{step, count};
    }
};
inline constexpr unified_fn unified{};

template<std::ptrdiff_t Step, std::ptrdiff_t Count>
    requires (Step > 0 && Count > 0)
struct unified_static_fn : adaptor_closure<unified_static_fn<Step, Count>> {
    template<std::ranges::viewable_range R>
    static constexpr auto operator()(R&& range) {
        return unified(std::forward<R>(range), Step, Count);
    }
};
template<std::ptrdiff_t Step, std::ptrdiff_t Count>
    requires (Step > 0 && Count > 0)
inline constexpr unified_static_fn<Step, Count> unified_c{};
} // namespace views
} // namespace neo

namespace neo::traits {
template<class T> struct is_unified_view : std::false_type {};
template<class V> struct is_unified_view<neo::unified_view<V>> : std::true_type {};
template<class T>
inline constexpr bool is_unified_view_v = is_unified_view<std::remove_cvref_t<T>>::value;

template<class T> struct unified_view_traits;
template<class V> struct unified_view_traits<neo::unified_view<V>> {
    using base_range = V;
    static constexpr bool read_only = true;
    static constexpr bool indexed = std::ranges::random_access_range<V> && std::ranges::sized_range<V>;
    static constexpr bool bidirectional = indexed ||
        (std::ranges::bidirectional_range<V> && std::ranges::common_range<V>);
    static constexpr bool forward_only = std::ranges::forward_range<V> && !bidirectional;
};
}

// The iterators hold their underlying iterators independently of the view.
namespace std::ranges {
template<class V>
inline constexpr bool enable_borrowed_range<::neo::unified_view<V>> = enable_borrowed_range<V>;
}
