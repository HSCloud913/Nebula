#include <gtest/gtest.h>
#include "Memory/Allocator/PoolAllocator.h"
#include <thread>
#include <vector>

using namespace ne::memory;

TEST(PoolAllocatorTest, AllocateAndDeallocate)
{
    PoolAllocator pool(64, 8);
    EXPECT_EQ(pool.Available(), 8u);

    void* p = pool.Allocate(64, 8);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(pool.Available(), 7u);

    pool.Deallocate(p, 64);
    EXPECT_EQ(pool.Available(), 8u);
}

TEST(PoolAllocatorTest, ExhaustPool)
{
    PoolAllocator pool(32, 4);
    void* ptrs[4]{};
    for (auto& ptr : ptrs) ptr = pool.Allocate(32, 4);

    EXPECT_EQ(pool.Available(), 0u);
    EXPECT_EQ(pool.Allocate(32, 4), nullptr);

    pool.Deallocate(ptrs[0], 32);
    EXPECT_EQ(pool.Available(), 1u);
}

TEST(PoolAllocatorTest, ThreadSafeMultiAlloc)
{
    PoolAllocator pool(16, 64);
    std::vector<std::thread> threads;
    std::atomic<int> allocated{ 0 };

    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&]
        {
            void* p = pool.Allocate(16, 1);
            if (p)
            {
                allocated.fetch_add(1);
                pool.Deallocate(p, 16);
            }
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(pool.Available(), 64u);
}
