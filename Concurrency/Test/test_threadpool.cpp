//
// Created by hscloud on 26. 6. 26.
//

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "Concurrency/ThreadPool.h"



TEST(ThreadPoolTest, ExecutesSingleJob)
{
	ne::ThreadPool pool(1);
	std::atomic<bool> executed{ false };

	auto future = pool.Enqueue([&]() { executed = true; });
	future.get();

	EXPECT_TRUE(executed.load());
}

TEST(ThreadPoolTest, ExecutesAllJobs)
{
	ne::ThreadPool pool(4);
	constexpr int numJobs = 100;
	std::atomic<int> counter{ 0 };

	std::vector<std::future<void>> futures;
	for (int i = 0; i < numJobs; ++i) futures.push_back(pool.Enqueue([&]() { ++counter; }));

	for (auto& f : futures) f.get();
	EXPECT_EQ(counter.load(), numJobs);
}

TEST(ThreadPoolTest, EnqueueReturnValue)
{
	ne::ThreadPool pool(2);

	auto f1 = pool.Enqueue([]() { return 42; });
	auto f2 = pool.Enqueue([]() { return std::string("hello"); });

	EXPECT_EQ(f1.get(), 42);
	EXPECT_EQ(f2.get(), "hello");
}

TEST(ThreadPoolTest, EnqueueInvalidAfterShutdown)
{
	ne::ThreadPool pool(2);
	pool.Shutdown();

	auto future = pool.Enqueue([]() { return 1; });
	EXPECT_FALSE(future.valid());
}

TEST(ThreadPoolTest, DestructorWaitsForJobs)
{
	std::atomic<bool> executed{ false };
	{
		ne::ThreadPool pool(2);
		pool.Enqueue([&]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			executed = true;
		});
	}
	EXPECT_TRUE(executed.load());
}

TEST(ThreadPoolTest, ConcurrentEnqueue)
{
	ne::ThreadPool pool(4);
	constexpr int numProducers = 8;
	constexpr int jobsPerProducer = 50;
	std::atomic<int> counter{ 0 };

	std::vector<std::thread> producers;
	for (int i = 0; i < numProducers; ++i) { producers.emplace_back([&]() { for (int j = 0; j < jobsPerProducer; ++j) pool.Enqueue([&]() { ++counter; }); }); }
	for (auto& t : producers) t.join();

	pool.Shutdown();
	EXPECT_EQ(counter.load(), numProducers * jobsPerProducer);
}

TEST(ThreadPoolTest, DoubleShutdownSafe)
{
	ne::ThreadPool pool(2);
	pool.Shutdown();
	EXPECT_NO_FATAL_FAILURE(pool.Shutdown());
}
