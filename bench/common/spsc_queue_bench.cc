#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "include/common/spsc_queue.h"
#include "include/common/utils.h"

using namespace janus::common;

namespace {

constexpr size_t kQueueSize = 1024;  // power-of-two

// ─────────────────────────────────────────────────────────────────────────────
// 1. Single-threaded round-trip cost
//    Push N items then pop N items on the same thread.
//    Measures pure throughput with no cache-line ping-pong.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Spsc_SingleThread_Throughput(benchmark::State& state) {
    SpscQueue<uint64_t, kQueueSize> q;
    const size_t batch = kQueueSize / 2;  // stay well under capacity

    for (auto _ : state) {
        for (size_t i = 0; i < batch; ++i) {
            benchmark::DoNotOptimize(q.emplace(i));
        }

        uint64_t val{};
        for (size_t i = 0; i < batch; ++i) {
            benchmark::DoNotOptimize(q.pop(val));
        }
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(batch));
}

BENCHMARK(BM_Spsc_SingleThread_Throughput)->Unit(benchmark::kNanosecond);

// ─────────────────────────────────────────────────────────────────────────────
// 2. Single-item latency (single-threaded)
//    Push one item, pop one item.  Isolates per-operation overhead.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Spsc_SingleThread_OneItem(benchmark::State& state) {
    SpscQueue<uint64_t, kQueueSize> q;
    uint64_t val{};

    for (auto _ : state) {
        benchmark::DoNotOptimize(q.emplace(42ULL));
        benchmark::DoNotOptimize(q.pop(val));
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_Spsc_SingleThread_OneItem)->Unit(benchmark::kNanosecond);

// ─────────────────────────────────────────────────────────────────────────────
// 3. Cross-thread throughput (the real benchmark)
//    Producer and consumer on separate threads (and optionally pinned CPUs).
//    Reports throughput and sets the latency counter via items processed.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Spsc_CrossThread_Throughput(benchmark::State& state) {
    const int producer_cpu = static_cast<int>(state.range(0));
    const int consumer_cpu = static_cast<int>(state.range(1));

    SpscQueue<uint64_t, kQueueSize> q;
    std::atomic<bool> stop{false};
    std::atomic<bool> ready{false};

    // Consumer thread
    std::thread consumer([&] {
        if (consumer_cpu >= 0) {
            utils::pin_thread(consumer_cpu);
        }

        ready.store(true, std::memory_order_release);

        uint64_t val{};
        while (!stop.load(std::memory_order_acquire) || !q.is_empty()) {
            if (q.pop(val)) {
                benchmark::DoNotOptimize(val);
            }
        }
    });

    // Producer (benchmark thread)
    if (producer_cpu >= 0) {
        utils::pin_thread(producer_cpu);
    }

    while (!ready.load(std::memory_order_acquire)) {
    }

    uint64_t produced = 0;
    for (auto _ : state) {
        while (!q.emplace(produced)) {
        }
        ++produced;
    }

    stop.store(true, std::memory_order_release);
    consumer.join();

    state.SetItemsProcessed(static_cast<int64_t>(produced));
}

BENCHMARK(BM_Spsc_CrossThread_Throughput)
    ->Args({0, 1})  // pinned, same physical core (hyperthreads)
    ->Args({0, 2})  // pinned, likely different physical cores
    ->Unit(benchmark::kNanosecond)
    ->UseRealTime();  // wall time matters for cross-thread

// ─────────────────────────────────────────────────────────────────────────────
// 4. Round-trip latency (ping-pong)
//    Measures producer→consumer→producer latency for a single token.
//    This is the closest you can get to "how long does one hop take?"
// ─────────────────────────────────────────────────────────────────────────────
void BM_Spsc_PingPong_Latency(benchmark::State& state) {
    const int cpu_a = static_cast<int>(state.range(0));
    const int cpu_b = static_cast<int>(state.range(1));

    // Two queues: A→B and B→A
    SpscQueue<uint64_t, 2> a_to_b;
    SpscQueue<uint64_t, 2> b_to_a;

    std::atomic<bool> ready{false};
    std::atomic<bool> stop{false};

    // Thread B: echo everything it receives back to A
    std::thread thread_b([&] {
        if (cpu_b >= 0) {
            utils::pin_thread(cpu_b);
        }

        ready.store(true, std::memory_order_release);

        uint64_t val{};
        while (!stop.load(std::memory_order_acquire)) {
            if (a_to_b.pop(val)) {
                while (!b_to_a.emplace(val)) {
                }
            }
        }
    });

    if (cpu_a >= 0) {
        utils::pin_thread(cpu_a);
    }
    // Wait for B to start
    while (!ready.load(std::memory_order_acquire)) {
    }

    uint64_t val{};
    uint64_t seq = 0;
    for (auto _ : state) {
        while (!a_to_b.emplace(seq)) {
        }
        seq++;
        while (!b_to_a.pop(val)) {
        }
        assert(val == seq - 1);
    }

    stop.store(true, std::memory_order_release);
    // Unblock B if it's stuck waiting
    (void)a_to_b.emplace(0);
    thread_b.join();

    state.SetItemsProcessed(state.iterations() * 2);
}

BENCHMARK(BM_Spsc_PingPong_Latency)
    ->Args({0, 1})  // pinned, same physical core (hyperthreads)
    ->Args({0, 2})  // pinned, likely different physical cores
    ->Unit(benchmark::kNanosecond)
    ->UseRealTime();

// ─────────────────────────────────────────────────────────────────────────────
// 5. Large payload cost
//    Same cross-thread throughput but with a fat struct to expose move cost.
// ─────────────────────────────────────────────────────────────────────────────
struct FatItem {
    std::array<uint64_t, 8> data{};  // 64 bytes — one full cache line
};

void BM_Spsc_CrossThread_FatPayload(benchmark::State& state) {
    SpscQueue<FatItem, kQueueSize> q;
    const int producer_cpu = static_cast<int>(state.range(0));
    const int consumer_cpu = static_cast<int>(state.range(1));

    std::atomic<bool> ready{false};
    std::atomic<bool> stop{false};

    std::thread consumer([&] {
        if (consumer_cpu >= 0) {
            utils::pin_thread(consumer_cpu);
        }

        ready.store(true, std::memory_order_release);

        FatItem item{};
        while (!stop.load(std::memory_order_acquire) || !q.is_empty()) {
            if (q.pop(item)) {
                benchmark::DoNotOptimize(item);
            }
        }
    });

    if (producer_cpu >= 0) {
        utils::pin_thread(producer_cpu);
    }

    FatItem item{};
    item.data[0] = 1;

    while (!ready.load(std::memory_order_acquire)) {
    }

    uint64_t produced = 0;
    for (auto _ : state) {
        item.data[0] = produced;
        while (!q.emplace(item)) {
        }
        ++produced;
    }

    stop.store(true, std::memory_order_release);
    consumer.join();
    state.SetItemsProcessed(static_cast<int64_t>(produced));
}

BENCHMARK(BM_Spsc_CrossThread_FatPayload)->Args({0, 1})->Args({0, 2})->Unit(benchmark::kNanosecond)->UseRealTime();

}  // namespace

BENCHMARK_MAIN();
