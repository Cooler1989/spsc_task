// Stress tests with one real producer thread and one real consumer thread.
// Most valuable when built with -DSPSC_SANITIZER=thread (data races) or address (leaks/UAF).

#include "spsc/queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace {

using spsc::Queue;

#ifndef SPSC_STRESS_ITEMS
#define SPSC_STRESS_ITEMS 1'000'000
#endif
constexpr std::uint64_t kItems = SPSC_STRESS_ITEMS;

template <typename Fn>
void spin_until(Fn&& fn) {
    while (!fn()) {
        std::this_thread::yield();
    }
}

class ConcurrentQueue : public ::testing::TestWithParam<std::size_t> {};

TEST_P(ConcurrentQueue, DeliversEveryItemExactlyOnceInOrder) {
    Queue<std::uint64_t> q(GetParam());

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kItems; ++i) {
            spin_until([&] { return q.try_push(i); });
        }
    });

    // Record the first mismatch instead of asserting inside the loop, to keep failure output short.
    std::uint64_t mismatches = 0;
    std::uint64_t first_bad_index = 0;
    std::uint64_t first_bad_value = 0;
    for (std::uint64_t expected = 0; expected < kItems; ++expected) {
        std::uint64_t value = 0;
        spin_until([&] { return q.try_pop(value); });
        if (value != expected && mismatches++ == 0) {
            first_bad_index = expected;
            first_bad_value = value;
        }
    }
    producer.join();

    EXPECT_EQ(mismatches, 0u) << "first mismatch at index " << first_bad_index << ": got "
                              << first_bad_value;
    EXPECT_TRUE(q.empty());
}

TEST_P(ConcurrentQueue, FrontAndPopObserveFullyConstructedElements) {
    // Heap-owning payload: a publication bug shows up as a wrong value, a crash or an ASan report.
    Queue<std::unique_ptr<std::uint64_t>> q(GetParam());

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kItems; ++i) {
            auto item = std::make_unique<std::uint64_t>(i);
            spin_until([&] { return q.try_push(std::move(item)); });
        }
    });

    std::uint64_t mismatches = 0;
    for (std::uint64_t expected = 0; expected < kItems; ++expected) {
        std::unique_ptr<std::uint64_t>* head = nullptr;
        spin_until([&] { return (head = q.front()) != nullptr; });
        if (!*head || **head != expected) {
            ++mismatches;
        }
        q.pop();
    }
    producer.join();

    EXPECT_EQ(mismatches, 0u);
}

TEST_P(ConcurrentQueue, SizeStaysWithinBoundsWhileBothSidesRun) {
    const std::size_t capacity = GetParam();
    Queue<std::uint64_t> q(capacity);
    std::atomic<bool> done{false};
    std::atomic<std::uint64_t> out_of_bounds{0};

    // size() is callable from either side; check it from both while they race.
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kItems; ++i) {
            spin_until([&] { return q.try_push(i); });
            if (q.size() > capacity) {
                out_of_bounds.fetch_add(1, std::memory_order_relaxed);
            }
        }
        done.store(true, std::memory_order_release);
    });

    std::uint64_t popped = 0;
    while (!done.load(std::memory_order_acquire) || !q.empty()) {
        if (q.try_pop()) {
            ++popped;
        }
        if (q.size() > capacity) {
            out_of_bounds.fetch_add(1, std::memory_order_relaxed);
        }
    }
    producer.join();

    EXPECT_EQ(popped, kItems);
    EXPECT_EQ(out_of_bounds.load(), 0u);
}

// Capacity 1 maximises full/empty transitions; odd sizes exercise non-power-of-two wrap-around.
INSTANTIATE_TEST_SUITE_P(Capacities, ConcurrentQueue, ::testing::Values(1, 2, 3, 64, 1000, 4096),
                         [](const auto& param_info) { return "Capacity" + std::to_string(param_info.param); });

}  // namespace
