#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "include/common/constants.h"
#include "include/common/spsc_queue.h"
#include "include/common/utils.h"

using namespace janus::common;

namespace {

constexpr size_t kQueueSize = 16'777'216;  // power-of-two
constexpr size_t kIters = 10'000'000;

constexpr uint64_t kProducerCore = 0;
constexpr uint64_t kConsumerCore = 2;

template <size_t ItemSize> struct Item {
    std::array<uint8_t, ItemSize> data;
};

// Producer and consumer on the same core.
template <size_t ItemSize> void BM_Spsc_SingleThread_Throughput(benchmark::State& state) {
    SpscQueue<Item<ItemSize>, kQueueSize> q;
    Item<ItemSize> item{};

    for (auto _ : state) {
        q.emplace(item);
        q.pop(item);
    }
}

#define REGISTER_SINGLE_THREAD(Size) \
    BENCHMARK_TEMPLATE(BM_Spsc_SingleThread_Throughput, Size)->Iterations(kIters)->Unit(benchmark::kNanosecond)

REGISTER_SINGLE_THREAD(4);
REGISTER_SINGLE_THREAD(8);
REGISTER_SINGLE_THREAD(16);
REGISTER_SINGLE_THREAD(32);
REGISTER_SINGLE_THREAD(64);
REGISTER_SINGLE_THREAD(128);

//Producer and consumer on separate cores.
template <size_t ItemSize> void BM_Spsc_CrossThread_Throughput(benchmark::State& state) {

    SpscQueue<Item<ItemSize>, kQueueSize> q;
    alignas(kCacheLineSize) std::atomic<bool> stop{false};
    alignas(kCacheLineSize) std::atomic<bool> ready{false};

    // Consumer thread
    std::thread consumer([&] {
        if (kConsumerCore >= 0) {
            utils::pin_self_to_core(kConsumerCore);
        }

        ready.store(true, std::memory_order_release);

        Item<ItemSize> item{};
        while (!stop.load(std::memory_order_acquire)) {
            q.pop(item);
        }
    });

    // Producer
    if (kProducerCore >= 0) {
        utils::pin_self_to_core(kProducerCore);
    }

    while (!ready.load(std::memory_order_acquire)) {
    }

    Item<ItemSize> item{};
    for (auto _ : state) {
        q.emplace(item);
    }

    stop.store(true, std::memory_order_release);
    (void)q.try_emplace(item);
    consumer.join();
}

#define REGISTER_CROSS_THREAD(Size)                          \
    BENCHMARK_TEMPLATE(BM_Spsc_CrossThread_Throughput, Size) \
        ->Iterations(kIters)                                 \
        ->Unit(benchmark::kNanosecond)                       \
        ->UseRealTime()

REGISTER_CROSS_THREAD(4);
REGISTER_CROSS_THREAD(8);
REGISTER_CROSS_THREAD(16);
REGISTER_CROSS_THREAD(32);
REGISTER_CROSS_THREAD(64);
REGISTER_CROSS_THREAD(128);

// Measures producer->consume->producer latency for a single token.
template <size_t ItemSize> void BM_Spsc_CrossThread_RTT(benchmark::State& state) {

    // Two queues: A->B and B->A
    SpscQueue<Item<ItemSize>, 2> a_to_b;
    SpscQueue<Item<ItemSize>, 2> b_to_a;

    alignas(kCacheLineSize) std::atomic<bool> ready{false};
    alignas(kCacheLineSize) std::atomic<bool> stop{false};

    std::thread thread_b([&] {
        if (kConsumerCore >= 0) {
            utils::pin_self_to_core(kConsumerCore);
        }

        ready.store(true, std::memory_order_release);

        Item<ItemSize> item{};
        while (!stop.load(std::memory_order_acquire)) {
            a_to_b.pop(item);
            b_to_a.emplace(item);
        }
    });

    if (kProducerCore >= 0) {
        utils::pin_self_to_core(kProducerCore);
    }

    // Wait for B to start
    while (!ready.load(std::memory_order_acquire)) {
    }

    Item<ItemSize> item{};
    for (auto _ : state) {
        a_to_b.emplace(item);
        b_to_a.pop(item);
    }

    stop.store(true, std::memory_order_release);
    (void)a_to_b.try_emplace(item);
    thread_b.join();
}

#define REGISTER_PING_PONG(Size)                      \
    BENCHMARK_TEMPLATE(BM_Spsc_CrossThread_RTT, Size) \
        ->Iterations(kIters)                          \
        ->Unit(benchmark::kNanosecond)                \
        ->UseRealTime()

REGISTER_PING_PONG(4);
REGISTER_PING_PONG(8);
REGISTER_PING_PONG(16);
REGISTER_PING_PONG(32);
REGISTER_PING_PONG(64);
REGISTER_PING_PONG(128);

}  // namespace

BENCHMARK_MAIN();
