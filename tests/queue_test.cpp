#include "spsc/queue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using spsc::Queue;

// Counts live instances so tests can verify every constructed element is destroyed exactly once.
struct Tracked {
    static inline int live = 0;

    explicit Tracked(int v = 0) : value(v) { ++live; }
    Tracked(const Tracked& other) : value(other.value) { ++live; }
    Tracked(Tracked&& other) noexcept : value(other.value) { ++live; }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&) noexcept = default;
    ~Tracked() { --live; }

    int value;
};

class TrackedTest : public ::testing::Test {
protected:
    void SetUp() override { Tracked::live = 0; }
    void TearDown() override { EXPECT_EQ(Tracked::live, 0); }
};

TEST(QueueConstruction, ReportsRequestedCapacity) {
    Queue<int> q(5);
    EXPECT_EQ(q.capacity(), 5u);
    EXPECT_EQ(q.size(), 0u);
    EXPECT_TRUE(q.empty());
}

TEST(QueueConstruction, RejectsZeroCapacity) {
    EXPECT_THROW(Queue<int>(0), std::invalid_argument);
}

TEST(QueueConstruction, RejectsCapacityThatWouldOverflow) {
    EXPECT_THROW(Queue<int>(SIZE_MAX), std::length_error);
}

TEST(QueueBasics, PopFromEmptyFails) {
    Queue<int> q(2);
    int out = 42;
    EXPECT_FALSE(q.try_pop(out));
    EXPECT_EQ(out, 42);
    EXPECT_FALSE(q.try_pop().has_value());
    EXPECT_EQ(q.front(), nullptr);
}

TEST(QueueBasics, PreservesFifoOrder) {
    Queue<int> q(4);
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(q.try_push(i));
    }
    for (int i = 0; i < 4; ++i) {
        int out = -1;
        ASSERT_TRUE(q.try_pop(out));
        EXPECT_EQ(out, i);
    }
    EXPECT_TRUE(q.empty());
}

TEST(QueueBasics, PushToFullFailsAndLeavesQueueIntact) {
    Queue<int> q(3);
    ASSERT_TRUE(q.try_push(1));
    ASSERT_TRUE(q.try_push(2));
    ASSERT_TRUE(q.try_push(3));
    EXPECT_FALSE(q.try_push(4));
    EXPECT_EQ(q.size(), 3u);

    EXPECT_EQ(q.try_pop(), 1);
    EXPECT_TRUE(q.try_push(4));
    EXPECT_EQ(q.try_pop(), 2);
    EXPECT_EQ(q.try_pop(), 3);
    EXPECT_EQ(q.try_pop(), 4);
    EXPECT_FALSE(q.try_pop().has_value());
}

TEST(QueueBasics, CapacityOneAlternates) {
    Queue<int> q(1);
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(q.try_push(i));
        ASSERT_FALSE(q.try_push(i));
        ASSERT_EQ(q.try_pop(), i);
        ASSERT_FALSE(q.try_pop().has_value());
    }
}

TEST(QueueBasics, SizeTracksPushesAndPopsAcrossWrapAround) {
    constexpr std::size_t capacity = 3;
    Queue<int> q(capacity);
    int next_push = 0;
    int next_pop = 0;
    // Cycle the indices several times around the ring with varying fill levels.
    for (int round = 0; round < 20; ++round) {
        const std::size_t to_push = static_cast<std::size_t>(round) % capacity + 1;
        for (std::size_t i = 0; i < to_push; ++i) {
            ASSERT_TRUE(q.try_push(next_push++));
        }
        EXPECT_EQ(q.size(), to_push);
        while (auto value = q.try_pop()) {
            ASSERT_EQ(*value, next_pop++);
        }
        EXPECT_EQ(q.size(), 0u);
    }
    EXPECT_EQ(next_push, next_pop);
}

TEST(QueueBasics, FrontPeeksWithoutRemoving) {
    Queue<std::string> q(2);
    ASSERT_TRUE(q.try_push("a"));
    ASSERT_TRUE(q.try_push("b"));

    std::string* head = q.front();
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(*head, "a");
    EXPECT_EQ(q.front(), head);
    EXPECT_EQ(q.size(), 2u);

    head->append("!");  // Consumer may modify the element in place.
    EXPECT_EQ(q.try_pop(), "a!");

    ASSERT_NE(q.front(), nullptr);
    EXPECT_EQ(*q.front(), "b");
    q.pop();
    EXPECT_EQ(q.front(), nullptr);
}

TEST(QueueBasics, EmplaceForwardsConstructorArguments) {
    Queue<std::pair<int, std::string>> q(1);
    ASSERT_TRUE(q.try_emplace(7, "seven"));
    auto value = q.try_pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->first, 7);
    EXPECT_EQ(value->second, "seven");
}

TEST(QueueTypes, SupportsMoveOnlyTypes) {
    Queue<std::unique_ptr<int>> q(2);
    ASSERT_TRUE(q.try_push(std::make_unique<int>(1)));
    ASSERT_TRUE(q.try_emplace(new int(2)));

    std::unique_ptr<int> out;
    ASSERT_TRUE(q.try_pop(out));
    EXPECT_EQ(*out, 1);
    auto second = q.try_pop();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(**second, 2);
}

TEST(QueueTypes, SupportsNonDefaultConstructibleTypes) {
    struct NoDefault {
        explicit NoDefault(int v) : value(v) {}
        int value;
    };
    Queue<NoDefault> q(1);
    ASSERT_TRUE(q.try_emplace(3));
    auto out = q.try_pop();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->value, 3);
}

TEST(QueueTypes, RespectsOverAlignedTypes) {
    struct alignas(128) Wide {
        std::uint64_t value;
    };
    Queue<Wide> q(3);
    for (std::uint64_t i = 0; i < 3; ++i) {
        ASSERT_TRUE(q.try_push(Wide{i}));
        Wide* slot = q.front();
        ASSERT_NE(slot, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(slot) % alignof(Wide), 0u);
        q.pop();
    }
}

TEST_F(TrackedTest, PopDestroysElements) {
    Queue<Tracked> q(4);
    ASSERT_TRUE(q.try_emplace(1));
    ASSERT_TRUE(q.try_emplace(2));
    EXPECT_EQ(Tracked::live, 2);

    q.pop();
    EXPECT_EQ(Tracked::live, 1);

    Tracked out;
    ASSERT_TRUE(q.try_pop(out));
    EXPECT_EQ(out.value, 2);
    EXPECT_EQ(Tracked::live, 1);  // Only `out` remains.
}

TEST_F(TrackedTest, DestructorDestroysRemainingElementsAfterWrapAround) {
    {
        Queue<Tracked> q(3);
        // Move the indices past the end of the buffer so the leftovers straddle the wrap point.
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(q.try_emplace(i));
        }
        q.pop();
        q.pop();
        ASSERT_TRUE(q.try_emplace(3));
        ASSERT_TRUE(q.try_emplace(4));
        EXPECT_EQ(Tracked::live, 3);
    }
    EXPECT_EQ(Tracked::live, 0);
}

TEST_F(TrackedTest, FailedPushDoesNotConstructOrConsume) {
    Queue<Tracked> q(1);
    ASSERT_TRUE(q.try_emplace(1));
    Tracked value(2);
    EXPECT_FALSE(q.try_push(std::move(value)));
    EXPECT_EQ(Tracked::live, 2);
}

// Throws from the constructor/assignment on demand, to verify the queue's exception guarantees.
struct Throwing {
    static inline bool throw_on_copy = false;
    static inline bool throw_on_assign = false;

    explicit Throwing(int v) : value(v) {}
    Throwing(const Throwing& other) : value(other.value) {
        if (throw_on_copy) throw std::runtime_error("copy");
    }
    Throwing& operator=(const Throwing& other) {
        if (throw_on_assign) throw std::runtime_error("assign");
        value = other.value;
        return *this;
    }

    int value;
};

class ThrowingTest : public ::testing::Test {
protected:
    void SetUp() override { Throwing::throw_on_copy = Throwing::throw_on_assign = false; }
    void TearDown() override { SetUp(); }
};

TEST_F(ThrowingTest, ThrowingConstructorLeavesQueueUnchanged) {
    Queue<Throwing> q(2);
    ASSERT_TRUE(q.try_emplace(1));

    Throwing::throw_on_copy = true;
    const Throwing value(2);
    EXPECT_THROW(q.try_push(value), std::runtime_error);
    EXPECT_EQ(q.size(), 1u);

    Throwing::throw_on_copy = false;
    ASSERT_TRUE(q.try_push(value));
    EXPECT_EQ(q.try_pop()->value, 1);
    EXPECT_EQ(q.try_pop()->value, 2);
}

TEST_F(ThrowingTest, ThrowingAssignmentKeepsElementInQueue) {
    Queue<Throwing> q(1);
    ASSERT_TRUE(q.try_emplace(1));

    Throwing out(0);
    Throwing::throw_on_assign = true;
    EXPECT_THROW(q.try_pop(out), std::runtime_error);
    EXPECT_EQ(q.size(), 1u);

    Throwing::throw_on_assign = false;
    ASSERT_TRUE(q.try_pop(out));
    EXPECT_EQ(out.value, 1);
}

TEST_F(ThrowingTest, ThrowingMoveOnOptionalPopKeepsElementInQueue) {
    Queue<Throwing> q(1);
    ASSERT_TRUE(q.try_emplace(1));

    Throwing::throw_on_copy = true;  // No move constructor, so the copy constructor is used.
    EXPECT_THROW(static_cast<void>(q.try_pop()), std::runtime_error);
    EXPECT_EQ(q.size(), 1u);

    Throwing::throw_on_copy = false;
    EXPECT_EQ(q.try_pop()->value, 1);
}

TEST(QueueNoexcept, PropagatesElementTypeGuarantees) {
    Queue<int> ints(1);
    static_assert(noexcept(ints.try_push(1)));
    static_assert(noexcept(ints.try_pop()));

    Queue<Throwing> throwing(1);
    const Throwing value(0);
    static_assert(!noexcept(throwing.try_push(value)));
    static_assert(!noexcept(throwing.try_pop()));
}

}  // namespace
