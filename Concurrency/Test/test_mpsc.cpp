#include <gtest/gtest.h>
#include "Concurrency/Queue/MpscQueue.h"
#include <thread>
#include <vector>
#include <numeric>
#include <algorithm>

using namespace ne::concurrency;

TEST(MpscQueueTest, SingleProducerSingleConsumer)
{
    MpscQueue<int> q;
    EXPECT_TRUE(q.IsEmpty());

    q.Enqueue(1);
    q.Enqueue(2);
    q.Enqueue(3);
    EXPECT_FALSE(q.IsEmpty());

    int v{};
    EXPECT_TRUE(q.Dequeue(v)); EXPECT_EQ(v, 1);
    EXPECT_TRUE(q.Dequeue(v)); EXPECT_EQ(v, 2);
    EXPECT_TRUE(q.Dequeue(v)); EXPECT_EQ(v, 3);
    EXPECT_FALSE(q.Dequeue(v));
    EXPECT_TRUE(q.IsEmpty());
}

TEST(MpscQueueTest, MultiProducerSingleConsumer)
{
    MpscQueue<int> q;
    constexpr int PerThread = 1000;
    constexpr int Threads   = 4;

    std::vector<std::thread> producers;
    for (int t = 0; t < Threads; ++t)
    {
        producers.emplace_back([&, t]
        {
            for (int i = 0; i < PerThread; ++i)
                q.Enqueue(t * PerThread + i);
        });
    }
    for (auto& t : producers) t.join();

    std::vector<int> collected;
    collected.reserve(Threads * PerThread);
    int v{};
    while (q.Dequeue(v)) collected.push_back(v);

    EXPECT_EQ(static_cast<int>(collected.size()), Threads * PerThread);

    std::sort(collected.begin(), collected.end());
    for (int i = 0; i < Threads * PerThread; ++i)
        EXPECT_EQ(collected[i], i);
}
