# neo STL

**An experimental C++23 library for policy-driven containers, generic algorithms, and composable ranges.**

`neo` explores a simple interface backed by explicit customization: ordinary code
uses `neo::vector` and `neo::sort` without configuration, while library authors
can select storage, allocation, iterator, growth, and exception-safety behavior
at compile time. Algorithms also support opt-in customization through ADL and
`tag_invoke`.

> **Status:** API-design prototype. It is **not** a drop-in or production-ready
> replacement for the C++ standard library. See [Guarantees and limitations](#guarantees-and-limitations)
> before using it outside experiments.

## Quick start

```cpp
#include "neo.hpp"

#include <array>
#include <ranges>

int main() {
    neo::vector numbers{5, 1, 4, 2, 3}; // CTAD: neo::vector<int>
    numbers.emplace_back(6);
    numbers.append_range(std::array{8, 7});

    neo::sort(numbers);
    auto first_ge_4 = neo::lower_bound(numbers, 4);

    auto transformed = numbers
        | neo::views::unified(2, 2) // take every second element, repeat twice
        | std::views::transform([](int x) { return x * 10; });

    auto owned = std::ranges::to<neo::vector<int>>(transformed);
    return first_ge_4 == numbers.end() || owned.empty();
}
```

The library is header-only. Include `neo.hpp` from the project root and compile
in C++23 mode. `std::ranges::to` requires a standard library that implements
that C++23 feature.

### Build and test

Requirements: CMake 3.20+, a C++23-capable compiler and standard library.
The project has been exercised with GCC 14 and Clang 17; availability of
individual C++23 library features depends on the chosen standard library.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For GCC/Clang AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build-san -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DNEO_SANITIZERS=ON
cmake --build build-san --parallel
ctest --test-dir build-san --output-on-failure
```

To consume the headers from another CMake project, add this source tree as a
subdirectory and link its `neo` **INTERFACE** target:

```cmake
add_subdirectory(path/to/neo-stl)
target_link_libraries(my_app PRIVATE neo)
```

No install/exported CMake package is provided yet.

## Container API

`neo::vector<T, Policies...>` is an owning, contiguous, resizable sequence **by
default**. Policies can change its storage/ownership and growth semantics.
The API deliberately uses the same methods for different configurations.

```cpp
neo::vector values{1, 2, 3};
values.emplace_back(4);
values.push_back(5);
values.append_range(std::array{6, 7});
values.reserve(64);
values.reserve_static<128>();

values.at(0);           // always bounds-checked
values[0];              // behavior depends on bounds policy
values.front();
values.back();
values.data();
values.span();          // std::span over the active elements
values.begin();
values.end();
values.rbegin();
values.rend();
values.clear();
```

Other supported operations include `pop_back`, `erase_if`, `assign_range`,
`get_allocator`, `size`, `capacity`, `max_size`, `empty`, and
`using_inline_storage`. Construction supports initializer lists, count,
iterator/sentinel pairs, and input ranges:

```cpp
std::array source{1, 2, 3};
neo::vector a{neo::from_range, source};
auto b = std::ranges::to<neo::vector>(source); // CTAD

auto c = source
    | std::views::transform([](int x) { return x * 2; })
    | std::ranges::to<neo::vector<int>>();
```

`neo::from_range` aliases `std::from_range` where available and otherwise
provides a compatibility tag. `std::ranges::to` itself is the **standard**
facility; neo does not implement a duplicate `neo::to`.

### Storage, allocation, and behavior policies

Specify only the policies that differ from the defaults; their ordering does
not matter.

| Category | Available policies | Purpose |
| --- | --- | --- |
| Storage | `storage::heap`, `small<N>`, `fixed<N>`, `external` | Heap, small-buffer, fixed inline, or caller-supplied memory |
| Allocation | `allocation::standard`, `pmr`, `with<A>` | Default, polymorphic, or custom allocator |
| Growth | `growth::none`, `exact`, `geometric<Num, Den>`, `custom<F>` | Capacity selection on growth |
| Relocation | `relocation::move_if_noexcept`, `always_move`, `copy` | How existing elements are relocated |
| Bounds | `bounds::unchecked`, `checked` | `operator[]` checking |
| Iteration | `iteration::raw`, `checked`, `checked_prefix` | Iterator representation and diagnostics |
| Exception safety | `exception_safety::basic`, `strong_growth` | Transactional growth for supported element types |

For example, a small-buffer vector with PMR allocation, checked access,
and prefix-preserving checked iterators:

```cpp
#include <memory_resource>

using Tasks = neo::vector<
    int,
    neo::storage::small<32>,
    neo::allocation::pmr,
    neo::bounds::checked,
    neo::iteration::checked_prefix,
    neo::exception_safety::strong_growth
>;

std::pmr::monotonic_buffer_resource arena;
Tasks tasks{&arena};
tasks.emplace_back(42);
```

`fixed<N>` and `external` cannot grow beyond their capacity. `small<N>` starts
with inline storage and can spill to heap. `external` uses caller-provided
**raw** memory; the caller retains ownership of the buffer, while the vector
manages the lifetime of its constructed elements. The buffer must satisfy
alignment and lifetime requirements.

A growth policy may also be a stateless callable NTTP:

```cpp
using PowerOfTwo = neo::growth::custom<
    [](std::size_t, std::size_t required) {
        return std::bit_ceil(required);
    }
>;
neo::vector<int, PowerOfTwo> values;
```

### Named presets and composition

Presets are ordinary, composable `policy_pack` aliases:

| Preset | Configuration |
| --- | --- |
| `presets::small<N>` | Small-buffer storage |
| `presets::fixed<N>` | Fixed-capacity storage |
| `presets::checked` | Checked bounds + checked prefix-preserving iterators |
| `presets::pmr` | PMR allocator |
| `presets::safe_small<N>` | Small storage + checked + strong growth |
| `presets::safe_fixed<N>` | Fixed storage + checked |
| `presets::pmr_small<N>` | Small storage + PMR |

```cpp
neo::vector<int, neo::presets::safe_small<32>> safe;

using Profile = neo::with_policies_t<
    neo::presets::safe_small<32>,
    neo::growth::exact,
    neo::allocation::pmr,
    neo::bounds::unchecked
>;

std::pmr::monotonic_buffer_resource resource;
neo::vector<int, Profile> configured{&resource};
```

A direct leaf policy overrides the corresponding category inherited from a
preset. Repeated policies of the same category at the same level are rejected
by constraints rather than silently choosing one. When storage changes, an
*unspecified* growth policy is re-derived from the new storage type.

For a reusable application-specific preset, specialize `neo::preset_traits`:

```cpp
struct debug_profile {};

template<>
struct neo::preset_traits<debug_profile> {
    using policies = neo::policy_pack<
        neo::storage::small<16>,
        neo::bounds::checked,
        neo::iteration::checked_prefix
    >;
};

using DebugVector = neo::vector<int, debug_profile>;
```

See `policy_pack_tests.cpp` and `presets_tests.cpp` for conflict rules and
negative compile-time tests.

## Iterators and ranges

`neo::vector` models `std::ranges::contiguous_range`, including when checked
iterators are enabled. It also supplies `const_iterator`, reverse iterators,
`data()` and `span()`.

| Iterator policy | Behavior |
| --- | --- |
| `raw` | Pointer iterators, no invalidation diagnostics |
| `checked` | Detects structural invalidation through generation tracking |
| `checked_prefix` | Preserves iterators to existing elements after append **without reallocation**; invalidates the old end iterator |

Checked iterators are a **debugging aid**, not lifetime-safe handles: a stale
iterator after destruction of its container is not safe to use. Invalidation
on other structural mutations is conservative. See
`iterator_contract_tests.cpp`.

### `unified_view`

`neo::views::unified(step, count)` selects every `step`-th source element and
repeats it `count` times. Both arguments must be positive.

```cpp
neo::vector values{1, 2, 3, 4, 5, 6};

auto view = values | neo::views::unified(2, 3);
// 1, 1, 1, 3, 3, 3, 5, 5, 5

auto fixed_parameters = values | neo::views::unified_c<2, 3>;
auto direct = neo::views::unified(values, 2, 3);
```

The view is lazy and exposes **read-only elements** (repeating writable
references would make mutation ambiguous). Its iterator capabilities depend
on the source:

| Source range | View capability |
| --- | --- |
| Sized random-access | Random-access + sized |
| Common bidirectional | Bidirectional |
| Forward-only | Forward |
| Non-common bidirectional | Forward + sentinel |
| Input-only | Input + sentinel |

The view does not model a contiguous range. As with standard non-owning views,
its source must outlive the view unless the view owns its source.

### Adaptor closures

`neo::views::adaptor_closure<Derived>` is the extension point for custom
pipelines. Neo/Neo composition is supported. When the standard library
provides C++23 `std::ranges::range_adaptor_closure`, mixed compositions with
`std::views` are also supported; the compatibility fallback only guarantees
`range | neo` and `neo | neo`.

```cpp
struct negate_fn : neo::views::adaptor_closure<negate_fn> {
    template<std::ranges::viewable_range R>
    constexpr auto operator()(R&& range) const {
        return std::forward<R>(range)
            | std::views::transform([](int x) { return -x; });
    }
};

inline constexpr negate_fn negate{};

auto pipeline = neo::views::unified(2, 2) | negate;
auto result = values | pipeline;
```

## Algorithms and customization

Algorithms accept suitable standard ranges as well as neo containers. Ordering
uses projection + comparator objects, so the default call does not need to
know the element type.

```cpp
struct Task { int priority; };
neo::vector<Task> tasks;
tasks.emplace_back(8);
tasks.emplace_back(2);
tasks.emplace_back(5);

neo::sort(tasks, neo::by(&Task::priority));
neo::sort(tasks, neo::stable, neo::by(&Task::priority));
neo::sort<neo::sort_mode::stable>(tasks, neo::by(&Task::priority));

auto nth = neo::nth_element(tasks, 1, neo::by(&Task::priority));
```

| Operation | Semantics / requirements |
| --- | --- |
| `sort`, stable `sort` | Random-access sortable range, unless customized |
| `nth_element(range, k)` / `nth_element<K>(range)` | k-th element selection; random-access sized borrowed range; checks index |
| `partition(range, pred)` | In-place partition; mutable forward range |
| `partition(range, stable, pred)` | Stable partition; mutable bidirectional range |
| `rotate(range, middle)` | Rotate mutable forward range |
| `unique(range, eq, projection)` | Compact adjacent equal elements; does **not** reduce container size |
| `lower_bound` / `upper_bound` | Binary-search insertion boundaries in an already ordered range |

The wrapper algorithms that return iterators deliberately constrain
`borrowed_range` to avoid returning dangling iterators from temporary owning
ranges. Use lvalues or borrowed views when an iterator is needed.

### Customization-point dispatch

`neo::sort`, `neo::nth_element`, and `neo::partition` are public function
templates. Their implementation dispatches in this order:

1. A valid ADL-discovered `tag_invoke` overload for the operation tag and arguments.
2. A constrained generic fallback using `std::ranges` algorithms.

A recognized custom sort returns `void`; custom selection/partition return
the source iterator type. Custom implementations must preserve the respective
algorithm's postconditions; the compiler can validate signatures and concepts,
not semantic correctness.

```cpp
struct Bucket {
    std::vector<int> data;

    auto begin() { return data.begin(); }
    auto end()   { return data.end(); }

    friend void tag_invoke(
        neo::sort_tag<neo::sort_mode::fast>,
        Bucket& bucket,
        neo::ordering<> options) {
        std::ranges::sort(bucket.data, options.compare, options.projection);
    }
};

Bucket bucket{{3, 1, 2}};
neo::sort(bucket);             // hidden-friend customization
neo::sort(bucket, neo::stable); // ordinary fallback
```

The tags are `sort_tag<Mode>`, `nth_element_tag`, and
`partition_tag<false/true>`. A constrained hidden-friend `tag_invoke` may
participate only for supported projections, comparators or element types.
Return-type mismatches are ignored for customization dispatch (and may leave
no viable generic fallback). `nth_element` checks `k` before dispatch.

Two **function-object CPOs** expose container operations independently of
`neo::vector`'s member methods:

```cpp
neo::reserve(values, 128);
neo::append_range(values, std::array{8, 9});

std::vector<int> standard;
neo::append_range(standard, std::array{1, 2});
```

`reserve` first tries `tag_invoke(reserve_tag{}, container, n)` then the
container's `reserve(n)`. `append_range` first tries
`tag_invoke(append_range_tag{}, container, source)`, then a matching member
`append_range`, then element-wise `emplace_back` or `push_back`. The generic
append fallback provides only a basic exception-safety guarantee.

## Traits and concepts

Algorithms dispatch based on capabilities, not container names. Public
inspection and constraint APIs include:

- **Policy/container:** `traits::vector_traits<V>`,
  `traits::policy_pack_traits<Pack>`, `growing_policy_vector`,
  `non_growing_policy_vector`, `inline_buffer_vector`, `strong_growth_vector`,
  `prefix_preserving_append_vector`, `statically_reservable_vector`.
- **Ranges:** `traits::range_traits<R>`, `input_only_range`,
  `forward_only_range`, `common_bidirectional_range`,
  `sized_random_access_range`, `borrowed_contiguous_range`,
  `mutable_forward_range`.
- **Algorithm suitability:** `mutable_sortable_range`,
  `nth_selectable_range`, `partitionable_range`,
  `stable_partitionable_range`, `rotatable_range`,
  `unique_compactable_range`, `binary_searchable_range`.
- **Customization/interop:** `tag_invocable`, `nothrow_tag_invocable`,
  `sort_dispatchable`, `nth_dispatchable`, `partition_dispatchable`,
  `appendable_range`, `reservable_container`, `range_constructible`,
  `allocator_compatible`.

```cpp
using FastSmall = neo::vector<int, neo::presets::safe_small<16>>;

static_assert(neo::inline_buffer_vector<FastSmall>);
static_assert(neo::strong_growth_vector<FastSmall>);
static_assert(neo::prefix_preserving_append_vector<FastSmall>);
static_assert(neo::traits::vector_traits<FastSmall>::inline_capacity == 16);
```

Traits describe configuration and compile-time properties. They are **not**
proofs of runtime resource equivalence: `allocator_compatible` checks allocator
types and comparability, not equality of two allocator instances.

## Guarantees and limitations

- **Not production-ready.** The API and implementation are experimental and
  do not claim all `std::vector` allocator, iterator or exception guarantees.
- **`strong_growth` is narrow:** rollback is implemented for `reserve()` and
  a single `emplace_back()` requiring growth, with element-type/relocation
  constraints. It does not imply transactional `assign_range`, `erase_if`,
  copy/move assignment, or general multi-element append.
- **Aliasing:** `vector::append_range` and `assign_range` require their sources
  not to alias the destination. Their checks are not comprehensive runtime
  alias detectors.
- **Allocators:** custom and PMR allocators work for allocation and release,
  but full `allocator_traits` propagation, fancy pointers and
  allocator-aware construction of nested elements are incomplete.
- **Object lifetime:** inline storage uses union/byte storage and explicit
  element lifetime management. Some constexpr cases work for supported
  element types, but no general `constexpr` or formal UB-freedom claim is made.
- **Checked iterators:** invalidation checks do not make iterators safe after
  container destruction; concurrent mutation/access is not made thread-safe.
- **Missing container operations:** general `insert`/`insert_range`, `resize`,
  `shrink_to_fit`, positional `erase`, and a fully specified `swap`.
- **Limited algorithm surface:** this is an API experiment, not a complete
  implementation of `<algorithm>` or `<ranges>`.

## Source layout

| Path | Role |
| --- | --- |
| `neo.hpp` | Single public umbrella header |
| `include/neo/vector.hpp` | Container API, construction, mutation, access |
| `include/neo/policies.hpp` | Policy definitions, resolution, packs, presets |
| `include/neo/storage_engine.hpp` | Storage, allocation, element lifetime |
| `include/neo/iterators.hpp` | Checked iterator implementations |
| `include/neo/traits.hpp` | Capability introspection |
| `include/neo/concepts.hpp` | Public constraints and requirements |
| `include/neo/algorithms.hpp` | Generic algorithms and dispatch |
| `include/neo/cpo.hpp` | CPO tags, `tag_invoke`, container CPOs |
| `include/neo/range_adaptor.hpp` | Reusable adaptor-closure infrastructure |
| `include/neo/unified_view.hpp` | Repetition/striding view |
| `include/neo/range_tags.hpp` | C++23 range-construction tag compatibility |
| `*_tests.cpp`, `demo.cpp` | Examples, behavioral and compile-time tests |

For executable usage examples, begin with `demo.cpp`, `presets_tests.cpp`,
`ranges_to_tests.cpp`, `adaptor_closure_tests.cpp`, and
`cpo_dispatch_tests.cpp`.
