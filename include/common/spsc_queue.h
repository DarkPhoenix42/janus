#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "include/common/constants.h"

namespace janus::common {

template <typename T> union StorageItem {
    T item_;

    StorageItem() {}

    ~StorageItem() {}
};

template <typename T, size_t N> class SpscQueue {
private:
    // Common
    alignas(kCacheLineSize) std::vector<StorageItem<T>> data_{};

    // Producer
    // Both push_idx_ and cached_pop_idx_ are placed consecutively, possibly on the same cache line
    alignas(kCacheLineSize) std::atomic<size_t> push_idx_{0};
    size_t cached_pop_idx_{0};

    // Consumer
    // Both pop_idx_ and cached_push_idx_ are placed consecutively, possibly on the same cache line
    alignas(kCacheLineSize) std::atomic<size_t> pop_idx_{0};
    size_t cached_push_idx_{0};

public:
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(const SpscQueue& other) = delete;
    SpscQueue& operator=(SpscQueue&& other) = delete;

    SpscQueue() {
        static_assert(N >= 2, "Queue capacity must be greater than or equal to 2.");
        data_.resize(N);
    }

    ~SpscQueue() {
        // Clean up any remaining items in the queue
        if constexpr (!std::is_trivially_destructible_v<T>) {
            size_t cur_pop_idx = pop_idx_.load(std::memory_order_relaxed);
            size_t cur_push_idx = push_idx_.load(std::memory_order_acquire);

            while (cur_pop_idx != cur_push_idx) {
                std::destroy_at(std::addressof(data_[cur_pop_idx].item_));
                cur_pop_idx = (cur_pop_idx + 1) & (N - 1);
            }
        }
    }

    static size_t next_idx(size_t cur_idx) {
        // N is a power of 2
        if constexpr ((N & (N - 1)) == 0) {
            return (cur_idx + 1) & (N - 1);
        } else {
            return (cur_idx == N - 1) ? 0 : cur_idx + 1;
        }
    }

    template <typename... Args>
        requires std::constructible_from<T, Args&&...>
    [[nodiscard]] bool try_emplace(Args&&... args) {
        size_t cur_push_idx = push_idx_.load(std::memory_order_relaxed);
        size_t next_push_idx = next_idx(cur_push_idx);

        if (next_push_idx == cached_pop_idx_) {
            cached_pop_idx_ = pop_idx_.load(std::memory_order_acquire);

            if (next_push_idx == cached_pop_idx_) {
                return false;
            }
        }

        std::construct_at(std::addressof(data_[cur_push_idx].item_), std::forward<Args>(args)...);
        push_idx_.store(next_push_idx, std::memory_order_release);

        return true;
    }

    [[nodiscard]] bool try_push(const T& item) { return try_emplace(item); }

    [[nodiscard]] bool try_push(T&& item) { return try_emplace(std::move(item)); }

    [[nodiscard]] bool try_pop(T& out) {
        size_t cur_pop_idx = pop_idx_.load(std::memory_order_relaxed);

        if (cur_pop_idx == cached_push_idx_) {
            cached_push_idx_ = push_idx_.load(std::memory_order_acquire);

            if (cur_pop_idx == cached_push_idx_) {
                return false;
            }
        }

        T* ptr = std::addressof(data_[cur_pop_idx].item_);
        out = std::move(*ptr);
        std::destroy_at(ptr);

        size_t next_pop_idx = next_idx(cur_pop_idx);
        pop_idx_.store(next_pop_idx, std::memory_order_release);

        return true;
    }

    template <typename... Args>
        requires std::constructible_from<T, Args&&...>
    void emplace(Args&&... args) {
        size_t cur_push_idx = push_idx_.load(std::memory_order_relaxed);
        size_t next_push_idx = next_idx(cur_push_idx);

        while (next_push_idx == cached_pop_idx_) {
            cached_pop_idx_ = pop_idx_.load(std::memory_order_acquire);
        }

        std::construct_at(std::addressof(data_[cur_push_idx].item_), std::forward<Args>(args)...);
        push_idx_.store(next_push_idx, std::memory_order_release);
    }

    void push(const T& item) { emplace(item); }

    void push(T&& item) { emplace(std::move(item)); }

    void pop(T& out) {
        size_t cur_pop_idx = pop_idx_.load(std::memory_order_relaxed);

        while (cur_pop_idx == cached_push_idx_) {
            cached_push_idx_ = push_idx_.load(std::memory_order_acquire);
        }

        T* ptr = std::addressof(data_[cur_pop_idx].item_);
        out = std::move(*ptr);
        std::destroy_at(ptr);

        size_t next_pop_idx = next_idx(cur_pop_idx);
        pop_idx_.store(next_pop_idx, std::memory_order_release);
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept { return N - 1; }

    [[nodiscard]] bool is_empty() const noexcept {
        return pop_idx_.load(std::memory_order_acquire) == push_idx_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_full() const noexcept {
        size_t next_push_idx = next_idx(push_idx_.load(std::memory_order_acquire));
        return next_push_idx == pop_idx_.load(std::memory_order_acquire);
    }
};

}  // namespace janus::common
