// Throughput of spsc::Queue versus a mutex-protected ring buffer, one producer and one consumer.
// Usage: spsc_throughput [items]

#include "spsc/queue.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {

// Baseline: the simplest correct bounded queue.
template <typename T>
class MutexQueue {
public:
    explicit MutexQueue(std::size_t capacity) : buffer_(capacity) {}

    bool try_push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ == buffer_.size()) return false;
        buffer_[(head_ + size_) % buffer_.size()] = value;
        ++size_;
        return true;
    }

    bool try_pop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ == 0) return false;
        out = buffer_[head_];
        head_ = (head_ + 1) % buffer_.size();
        --size_;
        return true;
    }

private:
    std::mutex mutex_;
    std::vector<T> buffer_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};

template <typename QueueT>
double run(std::size_t capacity, std::uint64_t items) {
    QueueT q(capacity);
    std::uint64_t checksum = 0;

    const auto start = std::chrono::steady_clock::now();
    std::thread consumer([&] {
        std::uint64_t value = 0;
        for (std::uint64_t i = 0; i < items; ++i) {
            while (!q.try_pop(value)) {
            }
            checksum += value;
        }
    });
    for (std::uint64_t i = 0; i < items; ++i) {
        while (!q.try_push(i)) {
        }
    }
    consumer.join();
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

    if (checksum != items * (items - 1) / 2) {
        std::fprintf(stderr, "checksum mismatch\n");
        std::exit(EXIT_FAILURE);
    }
    return static_cast<double>(items) / elapsed.count() / 1e6;
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint64_t items = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 10'000'000;

    std::printf("%llu items, best of 3 runs, million ops/s\n", static_cast<unsigned long long>(items));
    std::printf("%10s %14s %14s\n", "capacity", "spsc::Queue", "mutex queue");
    for (const std::size_t capacity : {std::size_t{16}, std::size_t{1024}, std::size_t{65536}}) {
        double spsc_best = 0;
        double mutex_best = 0;
        for (int run_index = 0; run_index < 3; ++run_index) {
            spsc_best = std::max(spsc_best, run<spsc::Queue<std::uint64_t>>(capacity, items));
            mutex_best = std::max(mutex_best, run<MutexQueue<std::uint64_t>>(capacity, items));
        }
        std::printf("%10zu %14.1f %14.1f\n", capacity, spsc_best, mutex_best);
    }
}
