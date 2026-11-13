#pragma once
#include "policies.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace neo::detail {

template<class T, class Relocation>
decltype(auto) relocation_source(T& element) noexcept {
    if constexpr (std::same_as<Relocation, relocation::always_move>)
        return std::move(element);
    else if constexpr (std::same_as<Relocation, relocation::copy>)
        return static_cast<const T&>(element);
    else
        return std::move_if_noexcept(element);
}

template<class T, class Relocation>
void relocate_into(T* dest, T* source, std::size_t count) {
    std::size_t constructed = 0;
    try {
        for (; constructed < count; ++constructed)
            std::construct_at(dest + constructed,
                              relocation_source<T, Relocation>(source[constructed]));
    } catch (...) {
        if (constructed) std::destroy_n(dest, constructed);
        throw;
    }
}

template<class T, class A>
class allocator_buffer {
protected:
    [[no_unique_address]] A alloc_;
    using traits = std::allocator_traits<A>;
    constexpr allocator_buffer() requires std::default_initializable<A> = default;
    constexpr explicit allocator_buffer(const A& a) : alloc_(a) {}
    T* allocate(std::size_t count) { return traits::allocate(alloc_, count); }
    constexpr void deallocate(T* ptr, std::size_t count) noexcept {
        if (ptr) traits::deallocate(alloc_, ptr, count);
    }
public:
    A get_allocator() const noexcept(std::is_nothrow_copy_constructible_v<A>) { return alloc_; }
    bool compatible(const allocator_buffer& other) const
        noexcept(std::allocator_traits<A>::is_always_equal::value ||
                 noexcept(std::declval<const A&>() == std::declval<const A&>())) {
        if constexpr (traits::is_always_equal::value) return true;
        return alloc_ == other.alloc_;
    }
};

// A single union contains an actual T array (for constant evaluation) and
// aligned bytes (for runtime placement construction). Unlike an array of
// per-element unions, the active T-array branch supports T* arithmetic.
// In C++23 the constexpr branch is intentionally limited to trivial T.
template<class T, std::size_t N>
union inline_storage {
    alignas(T) std::byte empty_[N ? N * sizeof(T) : 1];
    T elements_[N ? N : 1];

    constexpr inline_storage() noexcept : empty_{} {
        if consteval {
            if constexpr (std::is_trivial_v<T> && std::default_initializable<T> &&
                          std::is_assignable_v<T&, T>) {
                // Built-in assignment starts the lifetime of the inactive
                // array union member. All N slots form one real T array.
                for (std::size_t i = 0; i < N; ++i)
                    elements_[i] = T{};
            }
        }
    }
    constexpr ~inline_storage() {}

    [[nodiscard]] constexpr void* slot(std::size_t i) noexcept {
        return empty_ + i * sizeof(T);
    }
    [[nodiscard]] constexpr T* data(std::size_t live) noexcept {
        if consteval {
            return elements_; // only valid for the supported constexpr T
        } else {
            T* address = reinterpret_cast<T*>(empty_);
            return live ? std::launder(address) : address;
        }
    }
    [[nodiscard]] constexpr const T* data(std::size_t live) const noexcept {
        if consteval {
            return elements_;
        } else {
            const T* address = reinterpret_cast<const T*>(empty_);
            return live ? std::launder(address) : address;
        }
    }
    template<class... Args>
    constexpr T* construct(std::size_t index, Args&&... args) {
        if consteval {
            // The complete T[N] array is already live in constant evaluation.
            return std::construct_at(elements_ + index, std::forward<Args>(args)...);
        } else {
            return std::construct_at(static_cast<T*>(slot(index)),
                                     std::forward<Args>(args)...);
        }
    }
};

template<class T, class Storage, class A> class storage_engine;

template<class T, class A>
class storage_engine<T, storage::heap, A> : private allocator_buffer<T,A> {
    using base = allocator_buffer<T,A>;
    T* pointer_ = nullptr;
    std::size_t capacity_ = 0;
public:
    storage_engine() requires std::default_initializable<A> = default;
    explicit storage_engine(const A& alloc) : base(alloc) {}
    storage_engine(const storage_engine&) = delete;
    ~storage_engine() { release(); }
    using base::get_allocator;
    [[nodiscard]] T* data(std::size_t /*live*/) const noexcept { return pointer_; }
    template<class... Args>
    T* construct(std::size_t index, Args&&... args) {
        return std::construct_at(pointer_ + index, std::forward<Args>(args)...);
    }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool using_inline() const noexcept { return false; }
    void release() noexcept {
        this->deallocate(pointer_, capacity_);
        pointer_ = nullptr; capacity_ = 0;
    }
    bool steal_from(storage_engine& other) {
        if (!this->compatible(other)) return false;
        pointer_ = std::exchange(other.pointer_, nullptr);
        capacity_ = std::exchange(other.capacity_, 0);
        return true;
    }
    // Transactional grow+append: commit only after every construction succeeds.
    // This is called only for relocation policies satisfying
    // transactional_relocation_v<T, Relocation>.
    template<class Relocation, class... Args>
    T* grow_and_construct(std::size_t live, std::size_t required, Args&&... args) {
        T* next = this->allocate(required);
        try {
            // New element first. If its construction throws, the source is intact.
            std::construct_at(next + live, std::forward<Args>(args)...);
        } catch (...) {
            this->deallocate(next, required);
            throw;
        }
        try {
            relocate_into<T, Relocation>(next, data(live), live);
        } catch (...) {
            std::destroy_at(next + live);
            this->deallocate(next, required);
            throw;
        }
        if (live) std::destroy_n(data(live), live);
        release();
        pointer_ = next; capacity_ = required;
        return next + live;
    }
    template<class Relocation>
    void grow(std::size_t live, std::size_t required) {
        T* next = this->allocate(required);
        try { relocate_into<T, Relocation>(next, pointer_, live); }
        catch (...) { this->deallocate(next, required); throw; }
        if (live) std::destroy_n(pointer_, live);
        release();
        pointer_ = next; capacity_ = required;
    }
};

template<class T, std::size_t N, class A>
class storage_engine<T, storage::small<N>, A> : private allocator_buffer<T,A> {
    using base = allocator_buffer<T,A>;
    inline_storage<T, N> inline_{};
    T* heap_ = nullptr;
    std::size_t heap_capacity_ = 0;
public:
    constexpr storage_engine() requires std::default_initializable<A> = default;
    constexpr explicit storage_engine(const A& alloc) : base(alloc) {}
    storage_engine(const storage_engine&) = delete;
    constexpr ~storage_engine() { release(); }
    using base::get_allocator;
    [[nodiscard]] constexpr T* data(std::size_t live) noexcept {
        return heap_ ? heap_ : inline_.data(live);
    }
    [[nodiscard]] constexpr const T* data(std::size_t live) const noexcept {
        return heap_ ? heap_ : inline_.data(live);
    }
    template<class... Args>
    constexpr T* construct(std::size_t index, Args&&... args) {
        if (heap_) return std::construct_at(heap_ + index, std::forward<Args>(args)...);
        return inline_.construct(index, std::forward<Args>(args)...);
    }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return heap_ ? heap_capacity_ : N; }
    [[nodiscard]] constexpr bool using_inline() const noexcept { return heap_ == nullptr; }
    constexpr void release() noexcept {
        this->deallocate(heap_, heap_capacity_);
        heap_ = nullptr; heap_capacity_ = 0;
    }
    bool steal_from(storage_engine& other) {
        if (!other.heap_ || !this->compatible(other)) return false;
        heap_ = std::exchange(other.heap_, nullptr);
        heap_capacity_ = std::exchange(other.heap_capacity_, 0);
        return true;
    }
    // Transactional grow+append: commit only after every construction succeeds.
    // This is called only for relocation policies satisfying
    // transactional_relocation_v<T, Relocation>.
    template<class Relocation, class... Args>
    T* grow_and_construct(std::size_t live, std::size_t required, Args&&... args) {
        T* next = this->allocate(required);
        try {
            // New element first. If its construction throws, the source is intact.
            std::construct_at(next + live, std::forward<Args>(args)...);
        } catch (...) {
            this->deallocate(next, required);
            throw;
        }
        try {
            relocate_into<T, Relocation>(next, data(live), live);
        } catch (...) {
            std::destroy_at(next + live);
            this->deallocate(next, required);
            throw;
        }
        if (live) std::destroy_n(data(live), live);
        release();
        heap_ = next; heap_capacity_ = required;
        return next + live;
    }
    template<class Relocation>
    void grow(std::size_t live, std::size_t required) {
        T* next = this->allocate(required);
        try { relocate_into<T, Relocation>(next, data(live), live); }
        catch (...) { this->deallocate(next, required); throw; }
        if (live) std::destroy_n(data(live), live);
        release();
        heap_ = next; heap_capacity_ = required;
    }
};

template<class T, std::size_t N, class A>
class storage_engine<T, storage::fixed<N>, A> {
    inline_storage<T, N> inline_{};
public:
    constexpr storage_engine() = default;
    constexpr explicit storage_engine(const A&) {}
    storage_engine(const storage_engine&) = delete;
    [[nodiscard]] constexpr T* data(std::size_t live) noexcept {
        return inline_.data(live);
    }
    [[nodiscard]] constexpr const T* data(std::size_t live) const noexcept {
        return inline_.data(live);
    }
    template<class... Args>
    constexpr T* construct(std::size_t index, Args&&... args) {
        return inline_.construct(index, std::forward<Args>(args)...);
    }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return N; }
    [[nodiscard]] constexpr bool using_inline() const noexcept { return true; }
    A get_allocator() const noexcept(std::is_nothrow_default_constructible_v<A>) { return {}; }
    constexpr void release() noexcept {}
    bool steal_from(storage_engine&) noexcept { return false; }
    template<class Relocation>
    void grow(std::size_t, std::size_t) { throw std::length_error("fixed capacity exceeded"); }
};

template<class T, class A>
class storage_engine<T, storage::external, A> {
    std::byte* bytes_ = nullptr; // Never treat unconstructed external storage as a live T.
    std::size_t capacity_ = 0;
public:
    storage_engine() = default;
    explicit storage_engine(const A&) {}
    storage_engine(const storage_engine&) = delete;
    [[nodiscard]] T* data(std::size_t live) const noexcept {
        auto* address = reinterpret_cast<T*>(bytes_);
        return live ? std::launder(address) : address;
    }
    template<class... Args>
    T* construct(std::size_t index, Args&&... args) {
        void* slot = bytes_ + index * sizeof(T);
        return std::construct_at(static_cast<T*>(slot), std::forward<Args>(args)...);
    }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool using_inline() const noexcept { return false; }
    A get_allocator() const noexcept(std::is_nothrow_default_constructible_v<A>) { return {}; }
    void bind(std::span<std::byte> bytes) {
        if (bytes.size_bytes() != 0 &&
            (reinterpret_cast<std::uintptr_t>(bytes.data()) % alignof(T)) != 0)
            throw std::invalid_argument("external buffer is misaligned");
        bytes_ = bytes.data();
        capacity_ = bytes.size_bytes() / sizeof(T);
    }
    void release() noexcept { bytes_ = nullptr; capacity_ = 0; }
    bool steal_from(storage_engine& other) noexcept {
        bytes_ = std::exchange(other.bytes_, nullptr);
        capacity_ = std::exchange(other.capacity_, 0);
        return true;
    }
    template<class Relocation>
    void grow(std::size_t, std::size_t) { throw std::length_error("external capacity exceeded"); }
};
}
