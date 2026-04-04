#include "include/common/spsc_queue.h"

#include <gtest/gtest.h>

#include <thread>

using janus::common::SpscQueue;

TEST(SpscQueueTest, SingleThreadedBasic) {
    SpscQueue<int, 4> queue;
    EXPECT_TRUE(queue.is_empty());

    EXPECT_TRUE(queue.emplace(10));
    EXPECT_TRUE(queue.emplace(20));
    EXPECT_TRUE(queue.emplace(30));
    
    // Queue size is 4, but due to how the ring buffer mod math works 
    // to distinguish full vs empty, it can hold exactly N-1 items.
    EXPECT_FALSE(queue.emplace(40)); 

    int val = 0;
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 10);
    
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 20);
    
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 30);
    
    EXPECT_FALSE(queue.pop(val)); // Queue should be empty now
}

TEST(SpscQueueTest, MultiThreadedStressTest) {
    constexpr size_t kNumItems = 500'000;
    SpscQueue<size_t, 1024> queue;

    std::thread producer([&queue]() {
        for (size_t i = 1; i <= kNumItems; ++i) {
            while (!queue.emplace(i)) {
            }
        }
    });

    std::thread consumer([&queue]() {
        size_t expected = 1;
        size_t val = 0;
        while (expected <= kNumItems) {
            if (queue.pop(val)) {
                ASSERT_EQ(val, expected) << "Data race detected! Read out of order.";
                expected++;
            }
        }
    });

    producer.join();
    consumer.join();
}