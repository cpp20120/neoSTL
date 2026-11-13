#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>

namespace neo {

enum class sort_mode { fast, stable };
struct stable_t {};
inline constexpr stable_t stable{};

template<sort_mode Mode> struct sort_tag {};
struct nth_element_tag {};
template<bool Stable> struct partition_tag {};
struct append_range_tag {};
struct reserve_tag {};

namespace detail {
// ADL-only customization. There is deliberately no catch-all definition.
void tag_invoke();

template<class Tag, class... Args>
concept tag_invocable = requires(Tag&& tag, Args&&... args) {
    tag_invoke(std::forward<Tag>(tag), std::forward<Args>(args)...);
};

template<class Tag, class... Args>
    requires tag_invocable<Tag, Args...>
using tag_invoke_result_t = decltype(tag_invoke(std::declval<Tag>(), std::declval<Args>()...));

template<class Tag, class... Args>
concept nothrow_tag_invocable = tag_invocable<Tag, Args...> &&
    noexcept(tag_invoke(std::declval<Tag>(), std::declval<Args>()...));

template<class Tag, class... Args>
concept void_tag_invocable = tag_invocable<Tag, Args...> &&
    std::same_as<tag_invoke_result_t<Tag, Args...>, void>;

template<class C, class R>
concept member_append_range = requires(C&& c, R&& r) {
    { std::forward<C>(c).append_range(std::forward<R>(r)) } -> std::same_as<void>;
};

template<class C, class R>
concept emplace_append_range = std::ranges::input_range<R> &&
    requires(C&& c, std::ranges::range_reference_t<R> element) {
        std::forward<C>(c).emplace_back(std::forward<decltype(element)>(element));
    };

template<class C, class R>
concept push_append_range = std::ranges::input_range<R> &&
    requires(C&& c, std::ranges::range_reference_t<R> element) {
        std::forward<C>(c).push_back(std::forward<decltype(element)>(element));
    };

template<class C>
concept member_reservable = requires(C&& c, std::size_t capacity) {
    { std::forward<C>(c).reserve(capacity) } -> std::same_as<void>;
};
} // namespace detail

template<class Tag, class... Args>
concept tag_invocable = detail::tag_invocable<Tag, Args...>;

template<class Tag, class... Args>
concept nothrow_tag_invocable = detail::nothrow_tag_invocable<Tag, Args...>;

template<class Tag, class... Args>
    requires tag_invocable<Tag, Args...>
using tag_invoke_result_t = detail::tag_invoke_result_t<Tag, Args...>;

// Container-operation CPOs: ADL customization > matching member > generic insertion.
// No CPO is added to namespace std, and users customize their own namespace/type.
struct append_range_fn {
    template<class C, std::ranges::input_range R>
        requires detail::void_tag_invocable<append_range_tag, C, R>
    constexpr void operator()(C&& c, R&& r) const
        noexcept(detail::nothrow_tag_invocable<append_range_tag, C, R>) {
        tag_invoke(append_range_tag{}, std::forward<C>(c), std::forward<R>(r));
    }

    template<class C, std::ranges::input_range R>
        requires (!detail::void_tag_invocable<append_range_tag, C, R> &&
                  detail::member_append_range<C, R>)
    constexpr void operator()(C&& c, R&& r) const
        noexcept(noexcept(std::forward<C>(c).append_range(std::forward<R>(r)))) {
        std::forward<C>(c).append_range(std::forward<R>(r));
    }

    template<class C, std::ranges::input_range R>
        requires (!detail::void_tag_invocable<append_range_tag, C, R> &&
                  !detail::member_append_range<C, R> &&
                  (detail::emplace_append_range<C, R> || detail::push_append_range<C, R>))
    constexpr void operator()(C&& c, R&& r) const {
        for (auto&& x : r) {
            if constexpr (detail::emplace_append_range<C, R>)
                c.emplace_back(std::forward<decltype(x)>(x));
            else
                c.push_back(std::forward<decltype(x)>(x));
        }
    }
};
inline constexpr append_range_fn append_range{};

struct reserve_fn {
    template<class C>
        requires detail::void_tag_invocable<reserve_tag, C, std::size_t>
    constexpr void operator()(C&& c, std::size_t n) const
        noexcept(detail::nothrow_tag_invocable<reserve_tag, C, std::size_t>) {
        tag_invoke(reserve_tag{}, std::forward<C>(c), n);
    }

    template<class C>
        requires (!detail::void_tag_invocable<reserve_tag, C, std::size_t> &&
                  detail::member_reservable<C>)
    constexpr void operator()(C&& c, std::size_t n) const
        noexcept(noexcept(std::forward<C>(c).reserve(n))) {
        std::forward<C>(c).reserve(n);
    }
};
inline constexpr reserve_fn reserve{};

} // namespace neo
