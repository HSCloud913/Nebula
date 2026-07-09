#include <gtest/gtest.h>
#include "Concurrency/Queue/SpscQueue.h"
#include <thread>

using namespace ne::concurrency;

TEST(SpscQueueTest, BasicPushPop)
{
    SpscQueue<int> q(8);
    EXPECT_TRUE(q.IsEmpty());
    EXPECT_FALSE(q.IsFull());

    EXPECT_TRUE(q.Enqueue(42));
    EXPECT_FALSE(q.IsEmpty());

    int v{};
    EXPECT_TRUE(q.Dequeue(v));
    EXPECT_EQ(v, 42);
    EXPECT_TRUE(q.IsEmpty());
}

TEST(SpscQueueTest, FullQueue)
{
    SpscQueue<int> q(4); // capacity 4: ring size 4, usable slots 3
    // 실제 사용 가능 슬롯은 capacity - 1 = 3
    EXPECT_TRUE(q.Enqueue(1));
    EXPECT_TRUE(q.Enqueue(2));
    EXPECT_TRUE(q.Enqueue(3));
    EXPECT_TRUE(q.IsFull());
    EXPECT_FALSE(q.Enqueue(4));
}

TEST(SpscQueueTest, ConcurrentProducerConsumer)
{
    SpscQueue<int> q(1024);
    constexpr int Count = 500;
    std::vector<int> received;
    received.reserve(Count);

    std::thread producer([&]
    {
        for (int i = 0; i < Count; ++i)
        {
            while (!q.Enqueue(i)) {}
        }
    });

    std::thread consumer([&]
    {
        int v{};
        while (static_cast<int>(received.size()) < Count)
        {
            if (q.Dequeue(v)) received.push_back(v);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(received.size()), Count);
    for (int i = 0; i < Count; ++i)
        EXPECT_EQ(received[i], i);
}
