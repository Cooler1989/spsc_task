#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace spsc {

// Fixed instead of std::hardware_destructive_interference_size: that constant
// is ABI-unstable (GCC warns on use) and 64 bytes is correct for x86-64 and most ARM cores.
inline constexpr std::size_t cache_line_size = 64;

/// Bounded, lock-free, wait-free single-producer / single-consumer FIFO queue.
///
/// Thread-safety contract:
///  - Producer-side members (try_push, try_emplace) may be called by at most one
///    thread at a time.
///  - Consumer-side members (try_pop, front, pop) may be called by at most one
///    thread at a time.
///  - Observers (size, empty, capacity) may be called from any thread; size()
///    and empty() are snapshots that can be stale by the time they return.
///  - Construction and destruction must not race with any other member call.
template <typename T>
class Queue {
    static_assert(std::is_nothrow_destructible_v<T>, "T must have a non-throwing destructor");

public:
    using value_type = T;
    using size_type = std::size_t;

    /// Creates a queue that holds at most `capacity` elements.
    /// Throws std::invalid_argument if capacity is 0, std::length_error if it is too large,
    /// std::bad_alloc if storage cannot be allocated.
    explicit Queue(size_type capacity)
        : slot_count_(checked_slot_count(capacity)), slots_(std::make_unique<Slot[]>(slot_count_)) {}

    ~Queue() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            const size_type tail = tail_.load(std::memory_order_acquire);
            for (size_type i = head_.load(std::memory_order_acquire); i != tail; i = next(i)) {
                slot(i)->~T();
            }
        }
    }

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&) = delete;
    Queue& operator=(Queue&&) = delete;

    // ---- Producer side ----------------------------------------------------

    /// Constructs an element in place from `args`.
    /// Returns false (and constructs nothing) if the queue is full.
    /// Strong exception guarantee: if T's constructor throws, the queue is unchanged.
    template <typename... Args>
    bool try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>) {
        static_assert(std::is_constructible_v<T, Args&&...>, "T is not constructible from Args");
        const size_type tail = tail_.load(std::memory_order_relaxed);
        const size_type next_tail = next(tail);
        if (next_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (next_tail == cached_head_) {
                return false;
            }
        }
        ::new (static_cast<void*>(slots_[tail].storage)) T(std::forward<Args>(args)...);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(value);
    }

    bool try_push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return try_emplace(std::move(value));
    }

    // ---- Consumer side ----------------------------------------------------

    /// Move-assigns the oldest element into `out` and removes it.
    /// Returns false (and leaves `out` untouched) if the queue is empty.
    /// If T's move assignment throws, the element stays in the queue.
    bool try_pop(T& out) noexcept(std::is_nothrow_move_assignable_v<T>) {
        T* const element = front();
        if (element == nullptr) {
            return false;
        }
        out = std::move(*element);
        pop();
        return true;
    }

    /// Removes and returns the oldest element, or std::nullopt if the queue is empty.
    /// Works for T that is not default-constructible or not move-assignable.
    /// If T's move constructor throws, the element stays in the queue.
    std::optional<T> try_pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        T* const element = front();
        if (element == nullptr) {
            return std::nullopt;
        }
        std::optional<T> result(std::move(*element));
        pop();
        return result;
    }

    /// Returns a pointer to the oldest element, or nullptr if the queue is empty.
    /// The pointer stays valid until the consumer calls pop()/try_pop().
    [[nodiscard]] T* front() noexcept {
        const size_type head = head_.load(std::memory_order_relaxed);
        if (head == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head == cached_tail_) {
                return nullptr;
            }
        }
        return slot(head);
    }

    /// Destroys the oldest element. Precondition: front() returned non-null.
    void pop() noexcept {
        const size_type head = head_.load(std::memory_order_relaxed);
        assert(head != tail_.load(std::memory_order_acquire) && "pop() on empty queue");
        slot(head)->~T();
        head_.store(next(head), std::memory_order_release);
    }

    // ---- Observers --------------------------------------------------------

    [[nodiscard]] size_type capacity() const noexcept { return slot_count_ - 1; }

    /// Number of stored elements, always within [0, capacity()]. Exact only while the other
    /// side is idle; under concurrent use it is a snapshot.
    [[nodiscard]] size_type size() const noexcept {
        const size_type head = head_.load(std::memory_order_acquire);
        const size_type tail = tail_.load(std::memory_order_acquire);
        return tail >= head ? tail - head : slot_count_ - head + tail;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    struct Slot {
        alignas(T) unsigned char storage[sizeof(T)];
    };

    static size_type checked_slot_count(size_type capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("spsc::Queue capacity must be greater than 0");
        }
        // One extra slot distinguishes "full" from "empty" without a shared counter.
        if (capacity > std::numeric_limits<size_type>::max() / sizeof(Slot) - 1) {
            throw std::length_error("spsc::Queue capacity is too large");
        }
        return capacity + 1;
    }

    [[nodiscard]] size_type next(size_type index) const noexcept {
        const size_type n = index + 1;
        return n == slot_count_ ? 0 : n;
    }

    [[nodiscard]] T* slot(size_type index) noexcept {
        return std::launder(reinterpret_cast<T*>(slots_[index].storage));
    }

    // Read-only after construction; shared by both sides.
    const size_type slot_count_;
    const std::unique_ptr<Slot[]> slots_;

    // Consumer-owned: written by the consumer, read by the producer.
    alignas(cache_line_size) std::atomic<size_type> head_{0};
    // Consumer-private copy of tail_, refreshed only when the queue looks empty.
    size_type cached_tail_{0};

    // Producer-owned: written by the producer, read by the consumer.
    alignas(cache_line_size) std::atomic<size_type> tail_{0};
    // Producer-private copy of head_, refreshed only when the queue looks full.
    // sizeof(Queue) is rounded up to cache_line_size, so neighbours cannot share this line.
    size_type cached_head_{0};
};

}  // namespace spsc
