# spsc — bounded single-producer / single-consumer queue

Header-only, lock-free (wait-free) bounded FIFO queue for exactly one producer thread and one
consumer thread. C++17, standard library only.

```cpp
#include "spsc/queue.hpp"

spsc::Queue<std::string> q(1024);          // capacity fixed at construction

// producer thread
if (!q.try_push("hello")) { /* full: retry, back off or drop */ }
q.try_emplace(3, 'x');                     // constructs "xxx" in place

// consumer thread
std::string s;
if (q.try_pop(s)) { /* got one */ }         // move-assigns into s
if (auto v = q.try_pop()) { /* *v */ }      // std::optional<T>, no default ctor needed
if (std::string* p = q.front()) { use(*p); q.pop(); }  // zero-copy peek, then discard
```

## API

| Side     | Member                         | Semantics                                                   |
|----------|--------------------------------|-------------------------------------------------------------|
| Producer | `bool try_push(const T&)`      | Copy in; `false` if full.                                   |
| Producer | `bool try_push(T&&)`           | Move in; `false` if full (argument is not moved-from then). |
| Producer | `bool try_emplace(Args&&...)`  | Construct in place; `false` if full.                        |
| Consumer | `bool try_pop(T&)`             | Move-assign oldest into argument; `false` if empty.         |
| Consumer | `std::optional<T> try_pop()`   | Remove and return oldest; `nullopt` if empty.               |
| Consumer | `T* front()`                   | Peek oldest; `nullptr` if empty.                            |
| Consumer | `void pop()`                   | Destroy oldest. Precondition: queue not empty.              |
| Any      | `capacity()`, `size()`, `empty()` | `size`/`empty` are snapshots while the other side runs.  |

All operations are non-blocking and complete in a bounded number of steps. `noexcept` is
propagated from `T`'s constructors/assignment.

## Design

- **Ring buffer of `capacity + 1` slots.** The extra slot distinguishes full (`next(tail) == head`)
  from empty (`tail == head`) without a shared counter, so each index has exactly one writer.
  The capacity is exactly what the user asked for (no power-of-two rounding); the cost is a
  compare-and-reset instead of a bit mask when wrapping.
- **Memory ordering.** The producer constructs the element, then publishes it with a
  `release` store to `tail_`; the consumer `acquire`-loads `tail_` before reading. Symmetrically,
  the consumer destroys the element, then `release`-stores `head_`; the producer `acquire`-loads
  `head_` before reusing the slot. Each side reads its own index with `relaxed`.
- **Cached opposite index.** Each side keeps a private copy of the other side's index and only
  re-reads the shared atomic when the queue looks full (producer) or empty (consumer). This keeps
  cache-line traffic between cores low in the steady state.
- **No false sharing.** `head_` and `tail_` (each with its side's cached copy) live on separate
  64-byte cache lines.
- **Uninitialized storage.** Slots are raw aligned bytes; elements are constructed on push and
  destroyed on pop, so `T` need not be default-constructible and unused slots hold no objects.
  Over-aligned `T` is supported (C++17 aligned `new`).
- **Exception safety.** If `T`'s constructor (push) or move (pop) throws, the index is not
  advanced and the queue is unchanged (strong guarantee). `T`'s destructor must not throw.

## Assumptions / known limitations

- Exactly one producer thread and one consumer thread at a time. This is a contract, not checked
  at run time; violating it is a data race. The roles may migrate between threads only with
  external synchronization (e.g. a thread join) in between.
- Construction and destruction must not overlap with any other call. The destructor destroys
  elements still in the queue.
- Non-blocking only: there is no blocking/timed wait. Callers choose their own policy (spin,
  `std::this_thread::yield()`, sleep, or an external condition variable/eventfd).
- No bulk push/pop, no iteration, not copyable or movable (the queue's address is shared by
  two threads, so moving it would be unsafe anyway).
- Capacity must be ≥ 1 (`std::invalid_argument` otherwise); storage is allocated once in the
  constructor (`std::bad_alloc`/`std::length_error` on failure).
- The cache line size is fixed to 64 bytes (`std::hardware_destructive_interference_size` is not
  ABI-stable across GCC builds). Platforms with 128-byte lines (e.g. Apple M-series) would still
  be correct, just with possible false sharing.
- Adjacent slots share cache lines for small `T`; at the moment the queue is nearly empty or
  nearly full, producer and consumer can touch the same line. This is inherent to a contiguous
  ring buffer.

## Build

Requirements: Linux, CMake ≥ 3.16, GCC or Clang with C++17. GoogleTest is taken from the system
(`find_package(GTest)`) or, if not found, downloaded by CMake (v1.15.2, hash-verified).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Options:

| Option                    | Default | Description                                              |
|---------------------------|---------|----------------------------------------------------------|
| `SPSC_BUILD_TESTS`        | `ON`    | Unit and stress tests.                                   |
| `SPSC_BUILD_BENCHMARKS`   | `ON`    | `spsc_throughput` benchmark.                             |
| `SPSC_SANITIZER`          | empty   | `thread`, `address`, `undefined` or e.g. `address,undefined`. |

To use the queue in another CMake project: `add_subdirectory(spsc)` and link `spsc::spsc`, or just
copy [include/spsc/queue.hpp](include/spsc/queue.hpp).

## Run the tests

```sh
ctest --test-dir build --output-on-failure
# or directly
./build/tests/spsc_tests
```

- [tests/queue_test.cpp](tests/queue_test.cpp) — single-threaded semantics: FIFO order, full/empty
  boundaries, wrap-around, `front`/`pop`, move-only / non-default-constructible / over-aligned
  types, object lifetime (every element destroyed exactly once, including in the destructor),
  exception guarantees and `noexcept` propagation.
- [tests/concurrency_test.cpp](tests/concurrency_test.cpp) — one producer and one consumer thread
  transferring 1M items for capacities 1, 2, 3, 64, 1000, 4096, checking order, exactly-once
  delivery and publication of heap-owning elements.

The stress tests are most useful under sanitizers (item count is reduced automatically):

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++ -DSPSC_SANITIZER=thread
cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure

cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSPSC_SANITIZER=address,undefined
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
```

## Benchmark

```sh
./build/bench/spsc_throughput [items]
```

Compares against a mutex-protected ring buffer with busy-retry on both sides. Example (Release,
GCC 13, x86-64, threads not pinned):

```
  capacity    spsc::Queue    mutex queue   (million ops/s)
        16           50.1            3.4
      1024           96.1            9.8
     65536          130.9           10.2
```

Numbers depend heavily on the CPU, core placement (pin with `taskset -c 2,4 ...`) and load.
