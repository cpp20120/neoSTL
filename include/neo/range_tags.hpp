#pragma once

#include <version>

namespace neo {
// std::ranges::to may exist without std::from_range_t (e.g. libstdc++ 14).
#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
using from_range_t = std::from_range_t;
inline constexpr auto from_range = std::from_range;
#else
struct from_range_t { explicit from_range_t() = default; };
inline constexpr from_range_t from_range{};
#endif
} // namespace neo
