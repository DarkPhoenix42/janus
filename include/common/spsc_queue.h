#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace janus::common {

#ifdef __cpp_lib_hardware_interference_size
constexpr size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
constexpr size_t kCacheLineSize = 64;
#endif

template <typename T> union StorageItem {
    T item_;

    StorageItem() {}

    ~StorageItem() {}
};

template <typename T, size_t N> class SpscQueue {
private:
    // Common
    alignas(kCacheLineSize) std::array<StorageItem<T>, N> data_{};

    // Producer
    // Both push_idx_ and cached_pop_idx_ are placed consecutively, possibly on the same cache line
    alignas(kCacheLineSize) std::atomic<size_t> push_idx_;
    size_t cached_pop_idx_{};

    // Consumer
    // Both pop_idx_ and cached_push_idx_ are placed consecutively, possibly on the same cache line
    alignas(kCacheLineSize) std::atomic<size_t> pop_idx_;
    size_t cached_push_idx_{};

public:
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(const SpscQueue& other) = delete;
    SpscQueue& operator=(SpscQueue&& other) = delete;

    SpscQueue() : push_idx_{0}, pop_idx_{0} {
        static_assert(N >= 2, "Queue capacity must be greater than or equal to 2.");
        static_assert((N & (N - 1)) == 0, "Queue capacity must be a power of two.");
    }

    ~SpscQueue() {
        // Clean up any remaining items in the queue
        size_t cur_pop_idx = pop_idx_.load(std::memory_order_relaxed);
        size_t cur_push_idx = push_idx_.load(std::memory_order_acquire);

        while (cur_pop_idx != cur_push_idx) {
            std::destroy_at(std::addressof(data_[cur_pop_idx].item_));
            cur_pop_idx = (cur_pop_idx + 1) & (N - 1);
        }
    }

    template <typename... Args> [[nodiscard]] bool emplace(Args&&... args) {
        size_t cur_push_idx = push_idx_.load(std::memory_order_relaxed);

        // Fast mod for powers of 2
        size_t next_push_idx = (cur_push_idx + 1) & (N - 1);

        if (next_push_idx == cached_pop_idx_) [[unlikely]] {
            cached_pop_idx_ = pop_idx_.load(std::memory_order_acquire);

            if (next_push_idx == cached_pop_idx_) {
                return false;
            }
        }

        std::construct_at(std::addressof(data_[cur_push_idx].item_), std::forward<Args>(args)...);
        push_idx_.store(next_push_idx, std::memory_order_release);

        return true;
    }

    [[nodiscard]] bool pop(T& out) {
        size_t cur_pop_idx = pop_idx_.load(std::memory_order_relaxed);

        if (cur_pop_idx == cached_push_idx_) [[unlikely]] {
            cached_push_idx_ = push_idx_.load(std::memory_order_acquire);

            if (cur_pop_idx == cached_push_idx_) {
                return false;
            }
        }

        T* ptr = std::addressof(data_[cur_pop_idx].item_);
        out = std::move(*ptr);
        std::destroy_at(ptr);

        // Fast mod for powers of 2
        size_t next_pop_idx = (cur_pop_idx + 1) & (N - 1);
        pop_idx_.store(next_pop_idx, std::memory_order_release);

        return true;
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept { return N - 1; }

    /// Only the reader/consumer thread can call this.
    [[nodiscard]] bool is_empty() const noexcept {
        return pop_idx_.load(std::memory_order_relaxed) == push_idx_.load(std::memory_order_acquire);
    }
    
    // Only the writer/producer thread can call this.
    [[nodiscard]] bool is_full() const noexcept {
        size_t next_push_idx = (push_idx_.load(std::memory_order_relaxed) + 1) & (N - 1);
        return next_push_idx == pop_idx_.load(std::memory_order_acquire);
    }
};

}  // namespace janus::common
