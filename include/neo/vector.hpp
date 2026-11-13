#pragma once

#include "storage_engine.hpp"
#include "iterators.hpp"
#include "traits.hpp"
#include "range_tags.hpp"
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace neo {
// A small proof of concept: policy categories are inferred from policy types,
// independent of parameter ordering. No object allocations for small/fixed.
template<class T, class... Policies>
    requires valid_vector_policies<T, Policies...> && std::destructible<T>
class vector {
    using config = detail::vector_config<T, Policies...>;
    using allocator_type_impl = typename config::allocator_type;
    using engine_type = detail::storage_engine<T, typename config::storage_type, allocator_type_impl>;
    using relocation_policy = typename config::relocation_type;
    static constexpr bool strong_growth =
        traits::exception_safety_traits<typename config::exception_safety_type>::strong_growth;
    engine_type storage_;
    std::size_t size_ = 0;
    static constexpr bool checked_iteration = traits::iteration_traits<typename config::iteration_type>::is_checked;
    static constexpr bool preserve_append_prefix =
        traits::iteration_traits<typename config::iteration_type>::preserves_prefix_on_nonrealloc_append;
    [[no_unique_address]] mutable std::conditional_t<checked_iteration,
        detail::iterator_state<T>, detail::no_iterator_state> iter_state_{};
    constexpr void refresh_iterators(bool appended_without_reallocation = false) noexcept {
        if constexpr (checked_iteration) {
            iter_state_.data = storage_.data(size_);
            iter_state_.size = size_;
            if constexpr (preserve_append_prefix) {
                if (appended_without_reallocation)
                    ++iter_state_.append_generation;
                else
                    ++iter_state_.generation;
            } else {
                (void)appended_without_reallocation;
                ++iter_state_.generation;
            }
        } else {
            (void)appended_without_reallocation;
        }
    }

    static constexpr bool external = std::same_as<typename config::storage_type, storage::external>;
    static constexpr bool checked = traits::bounds_traits<typename config::bounds_type>::is_checked;

    constexpr void ensure_size(std::size_t extra) const {
        if (extra > max_size() - size_)
            throw std::length_error("neo::vector size exceeds max_size");
    }
    [[nodiscard]] constexpr std::size_t growth_capacity(std::size_t required) const {
        auto desired = typename config::growth_type{}(capacity(), required);
        if (desired < required) desired = required;
        return std::min(desired, max_size());
    }
    constexpr void reserve_for(std::size_t extra) {
        ensure_size(extra);
        const auto required = size_ + extra;
        if (required <= capacity()) return;
        if constexpr (neo::is_non_growing_storage_v<typename config::storage_type>) {
            throw std::length_error("neo::vector cannot grow fixed/external storage");
        } else {
            reserve(growth_capacity(required));
        }
    }
    void transfer_from(vector& other) {
        if (storage_.steal_from(other.storage_)) {
            size_ = std::exchange(other.size_, 0);
            refresh_iterators(); other.refresh_iterators();
            return;
        }
        reserve_for(other.size_);
        for (std::size_t i = 0; i < other.size_; ++i) {
            storage_.construct(size_, detail::relocation_source<T, relocation_policy>(other.data()[i]));
            ++size_;
        }
        other.clear();
        refresh_iterators();
    }

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using allocator_type = allocator_type_impl;
    using iterator = std::conditional_t<checked_iteration,
        detail::checked_iterator<T, false, preserve_append_prefix>, T*>;
    using const_iterator = std::conditional_t<checked_iteration,
        detail::checked_iterator<T, true, preserve_append_prefix>, const T*>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using policy_config = config;

    constexpr vector() noexcept(std::is_nothrow_default_constructible_v<engine_type>)
        requires (!external && std::default_initializable<allocator_type>) { refresh_iterators(); }
    constexpr explicit vector(const allocator_type& allocator)
        noexcept(std::is_nothrow_constructible_v<engine_type, const allocator_type&>)
        requires (!external)
        : storage_(allocator) { refresh_iterators(); }
    explicit vector(std::pmr::memory_resource* resource)
        requires (!external && std::same_as<typename config::allocation_type, allocation::pmr>)
        : storage_(allocator_type{resource}) { refresh_iterators(); }
    vector(std::allocator_arg_t, const allocator_type& allocator) requires (!external)
        : vector(allocator) {}
    explicit vector(std::span<std::byte> raw_memory) requires (external) {
        storage_.bind(raw_memory);
        refresh_iterators();
    }
    vector(std::allocator_arg_t, const allocator_type& allocator, std::initializer_list<T> init)
        requires (!external && std::copy_constructible<T>) : storage_(allocator) {
        try { append_range(init); } catch (...) { clear(); throw; }
    }
    vector(std::initializer_list<T> init) requires (!external && std::copy_constructible<T>) {
        try { append_range(init); } catch (...) { clear(); throw; }
    }

    explicit vector(std::size_t count)
        requires (!external && std::default_initializable<T>) {
        try {
            reserve_for(count);
            for (std::size_t i = 0; i < count; ++i) emplace_back();
        } catch (...) { clear(); throw; }
    }

    // Iterator/sentinel construction also makes std::ranges::to<neo::vector>
    // able to deduce T via its iterator-pair CTAD fallback, independently of
    // whether the standard library has std::from_range_t yet.
    template<std::input_iterator I, std::sentinel_for<I> S>
        requires (!external && std::constructible_from<T, std::iter_reference_t<I>>)
    vector(I first, S last) {
        try {
            for (; first != last; ++first)
                emplace_back(*first);
        } catch (...) { clear(); throw; }
    }

    template<std::ranges::input_range R>
        requires (!external && std::constructible_from<T, std::ranges::range_reference_t<R>>)
    explicit vector(from_range_t, R&& source) {
        try { append_range(std::forward<R>(source)); } catch (...) { clear(); throw; }
    }

    // Match the standard range-constructor argument order: from_range,
    // range, allocator. The older allocator_arg overload remains supported.
    template<std::ranges::input_range R>
        requires (!external && std::constructible_from<T, std::ranges::range_reference_t<R>>)
    vector(from_range_t, R&& source, const allocator_type& allocator)
        : storage_(allocator) {
        try { append_range(std::forward<R>(source)); } catch (...) { clear(); throw; }
    }

    template<std::ranges::input_range R>
        requires (!external && std::constructible_from<T, std::ranges::range_reference_t<R>>)
    vector(std::allocator_arg_t, const allocator_type& allocator, from_range_t, R&& source)
        : storage_(allocator) {
        try { append_range(std::forward<R>(source)); } catch (...) { clear(); throw; }
    }

    vector(const vector& other) requires (!external && std::copy_constructible<T>)
        : storage_(other.get_allocator()) {
        try { append_range(other); } catch (...) { clear(); throw; }
    }
    vector(const vector&) requires (external || !std::copy_constructible<T>) = delete;

    vector(vector&& other) : storage_(other.get_allocator()) {
        try { transfer_from(other); } catch (...) { clear(); throw; }
    }
    constexpr ~vector() noexcept { clear(); }

    vector& operator=(const vector& other)
        requires (!external && std::copy_constructible<T>) {
        if (this != &other) {
            vector staged(other);
            *this = std::move(staged);
        }
        return *this;
    }
    vector& operator=(const vector&) requires (external || !std::copy_constructible<T>) = delete;

    vector& operator=(vector&& other) {
        if (this != &other) {
            clear();
            storage_.release();
            transfer_from(other);
            refresh_iterators();
        }
        return *this;
    }

    [[nodiscard]] allocator_type get_allocator() const noexcept(noexcept(storage_.get_allocator())) {
        return storage_.get_allocator();
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return storage_.capacity(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr bool using_inline_storage() const noexcept { return storage_.using_inline(); }
    [[nodiscard]] constexpr std::size_t max_size() const noexcept {
        if constexpr (neo::is_non_growing_storage_v<typename config::storage_type>)
            return capacity();
        else
            return static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(T);
    }
#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
    template<class Self>
    [[nodiscard]] constexpr auto data(this Self& self) noexcept
        -> std::conditional_t<std::is_const_v<Self>, const T*, T*> {
        return self.storage_.data(self.size_);
    }
    template<class Self>
    [[nodiscard]] constexpr auto begin(this Self& self) noexcept {
        if constexpr (checked_iteration) {
            using result = std::conditional_t<std::is_const_v<Self>, const_iterator, iterator>;
            return result{&self.iter_state_, 0};
        } else {
            return self.data();
        }
    }
    template<class Self>
    [[nodiscard]] constexpr auto end(this Self& self) noexcept {
        if constexpr (checked_iteration) {
            using result = std::conditional_t<std::is_const_v<Self>, const_iterator, iterator>;
            return result{&self.iter_state_, static_cast<std::ptrdiff_t>(self.size_)};
        } else {
            return self.size_ ? self.data() + self.size_ : self.data();
        }
    }
    template<class Self>
    [[nodiscard]] constexpr auto rbegin(this Self& self) noexcept {
        using result = std::conditional_t<std::is_const_v<Self>, const_reverse_iterator, reverse_iterator>;
        return result(self.end());
    }
    template<class Self>
    [[nodiscard]] constexpr auto rend(this Self& self) noexcept {
        using result = std::conditional_t<std::is_const_v<Self>, const_reverse_iterator, reverse_iterator>;
        return result(self.begin());
    }
    template<class Self>
    [[nodiscard]] constexpr auto span(this Self& self) noexcept {
        using element = std::conditional_t<std::is_const_v<Self>, const T, T>;
        return std::span<element>{self.data(), self.size_};
    }
    template<class Self>
    [[nodiscard]] constexpr decltype(auto) operator[](this Self& self, std::size_t i) noexcept(
        traits::bounds_traits<typename config::bounds_type>::indexed_access_is_nothrow) {
        if constexpr (checked) {
            if (i >= self.size_) throw std::out_of_range("neo::vector index");
        }
        return self.data()[i];
    }
    template<class Self>
    [[nodiscard]] constexpr decltype(auto) at(this Self& self, std::size_t i) {
        if (i >= self.size_) throw std::out_of_range("neo::vector index");
        return self.data()[i];
    }
    template<class Self>
    [[nodiscard]] constexpr decltype(auto) front(this Self& self) { return self.at(0); }
    template<class Self>
    [[nodiscard]] constexpr decltype(auto) back(this Self& self) { return self.at(self.size_ - 1); }
#else
    [[nodiscard]] constexpr T* data() noexcept { return storage_.data(size_); }
    [[nodiscard]] constexpr const T* data() const noexcept { return storage_.data(size_); }
    [[nodiscard]] constexpr iterator begin() noexcept {
        if constexpr (checked_iteration) return {&iter_state_, 0};
        else return data();
    }
    [[nodiscard]] constexpr iterator end() noexcept {
        if constexpr (checked_iteration) return {&iter_state_, static_cast<std::ptrdiff_t>(size_)};
        else return size_ ? data() + size_ : data();
    }
    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        if constexpr (checked_iteration) return {&iter_state_, 0};
        else return data();
    }
    [[nodiscard]] constexpr const_iterator end() const noexcept {
        if constexpr (checked_iteration) return {&iter_state_, static_cast<std::ptrdiff_t>(size_)};
        else return size_ ? data() + size_ : data();
    }
    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    [[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    [[nodiscard]] constexpr std::span<T> span() noexcept { return {data(), size_}; }
    [[nodiscard]] constexpr std::span<const T> span() const noexcept { return {data(), size_}; }

    [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept(
        traits::bounds_traits<typename config::bounds_type>::indexed_access_is_nothrow) {
        if constexpr (checked) {
            if (i >= size_) throw std::out_of_range("neo::vector index");
        }
        return data()[i];
    }
    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept(
        traits::bounds_traits<typename config::bounds_type>::indexed_access_is_nothrow) {
        if constexpr (checked) {
            if (i >= size_) throw std::out_of_range("neo::vector index");
        }
        return data()[i];
    }
    [[nodiscard]] constexpr T& at(std::size_t i) {
        if (i >= size_) throw std::out_of_range("neo::vector index");
        return data()[i];
    }
    [[nodiscard]] constexpr const T& at(std::size_t i) const {
        if (i >= size_) throw std::out_of_range("neo::vector index");
        return data()[i];
    }
    [[nodiscard]] constexpr T& front() { return at(0); }
    [[nodiscard]] constexpr const T& front() const { return at(0); }
    [[nodiscard]] constexpr T& back() { return at(size_ - 1); }
    [[nodiscard]] constexpr const T& back() const { return at(size_ - 1); }

#endif
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] constexpr const_iterator cend() const noexcept { return end(); }
    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

    constexpr void reserve(std::size_t desired) {
        if (desired <= capacity()) return;
        if (desired > max_size()) throw std::length_error("neo::vector reserve exceeds max_size");
        storage_.template grow<relocation_policy>(size_, desired);
        refresh_iterators();
    }

    // Invalid sizes are rejected during template argument substitution.
    // Compile-time extent is checked for fixed storage; runtime capacity is
    // still checked by reserve for dynamically supplied external buffers.
    template<std::size_t N>
        requires (!external &&
             (!neo::is_non_growing_storage_v<typename config::storage_type> ||
              N <= [] { if constexpr (requires { config::storage_type::inline_capacity; })
                           return config::storage_type::inline_capacity;
                       else return N; }()))
    constexpr void reserve_static() noexcept(neo::is_non_growing_storage_v<typename config::storage_type>) {
        reserve(N);
    }

    template<class... Args>
        requires std::constructible_from<T, Args...>
    constexpr T& emplace_back(Args&&... args) {
        if constexpr (neo::is_non_growing_storage_v<typename config::storage_type>) {
            // No temporary or relocation required, even for immovable T.
            if (size_ == capacity())
                throw std::length_error("neo::vector capacity exceeded");
        } else if (size_ == capacity()) {
            // Stage first so arguments referring to existing elements remain valid
            // across relocation. Move/copy of the staged value may still throw.
            T staged(std::forward<Args>(args)...);
            if constexpr (strong_growth) {
                ensure_size(1);
                const auto desired = growth_capacity(size_ + 1);
                T* added = storage_.template grow_and_construct<relocation_policy>(
                    size_, desired,
                    detail::relocation_source<T, relocation_policy>(staged));
                ++size_;
                refresh_iterators();
                return *added;
            } else {
                reserve_for(1);
                T* added = storage_.construct(size_,
                                detail::relocation_source<T, relocation_policy>(staged));
                ++size_;
                refresh_iterators(true);
                return *added;
            }
        }
        T* added = storage_.construct(size_, std::forward<Args>(args)...);
        ++size_;
        refresh_iterators(true);
        return *added;
    }

    template<class U>
        requires std::constructible_from<T, U>
    constexpr void push_back(U&& value) { emplace_back(std::forward<U>(value)); }

    constexpr void pop_back() {
        if (empty()) throw std::out_of_range("neo::vector pop_back on empty vector");
        std::destroy_at(data() + --size_);
        refresh_iterators();
    }
    constexpr void clear() noexcept {
        if (size_) std::destroy_n(data(), size_);
        size_ = 0;
        refresh_iterators();
    }

    template<std::ranges::input_range R>
        requires std::constructible_from<T, std::ranges::range_reference_t<R>>
    constexpr void append_range(R&& range) {
        // Precondition: the source must not alias this vector or any subrange of it.
        if constexpr (std::ranges::sized_range<R>) reserve_for(std::ranges::size(range));
        for (auto&& x : range) emplace_back(std::forward<decltype(x)>(x));
    }

    template<std::ranges::input_range R>
        requires std::constructible_from<T, std::ranges::range_reference_t<R>>
    void assign_range(R&& range) {
        // Like append_range, source must not alias the destination.
        // Basic guarantee: old contents are discarded before insertion.
        clear();
        append_range(std::forward<R>(range));
    }

    template<class Predicate>
        requires std::permutable<T*> && std::indirect_unary_predicate<Predicate, T*>
    std::size_t erase_if(Predicate pred) {
        if (empty()) return 0;
        const auto tail = std::ranges::remove_if(begin(), end(), std::move(pred));
        auto remaining = static_cast<std::size_t>(tail.begin() - begin());
        const auto count = size_ - remaining;
        if (count) std::destroy_n(data() + remaining, count);
        size_ = remaining;
        if (count) refresh_iterators();
        return count;
    }
};

template<class T>
vector(std::initializer_list<T>) -> vector<T>;

template<std::ranges::input_range R>
vector(from_range_t, R&&) -> vector<std::ranges::range_value_t<R>>;

// Required for std::ranges::to<neo::vector> on libraries that deduce a
// container template from a synthesized input iterator pair.
template<std::input_iterator I, std::sentinel_for<I> S>
vector(I, S) -> vector<std::iter_value_t<I>>;
}
