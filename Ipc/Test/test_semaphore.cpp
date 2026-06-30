//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>
#include <thread>

#include "../../Ipc/Semaphore.h"

using ne::ipc::Semaphore;

TEST(SemaphoreTest, AcquireReleaseCycle)
{
	auto semaphore = Semaphore("nebula-sem-test-cycle", 1);

	semaphore.Acquire();
	EXPECT_FALSE(semaphore.TryAcquire());

	semaphore.Release();
	EXPECT_TRUE(semaphore.TryAcquire());
}

TEST(SemaphoreTest, CountingSemaphoreAllowsMultipleAcquires)
{
	auto semaphore = Semaphore("nebula-sem-test-counting", 2);

	EXPECT_TRUE(semaphore.TryAcquire());
	EXPECT_TRUE(semaphore.TryAcquire());
	EXPECT_FALSE(semaphore.TryAcquire());

	semaphore.Release(2);
	EXPECT_TRUE(semaphore.TryAcquire());
}

TEST(SemaphoreTest, MutualExclusionAcrossThreads)
{
	auto semaphore = Semaphore("nebula-sem-test-mutex", 1);
	auto counter = 0;
	auto raceDetected = false;

	const auto worker = [&]
	{
		for (auto i = 0; i < 1000; ++i)
		{
			semaphore.Acquire();
			const auto before = counter;
			counter = before + 1;
			if (counter != before + 1) raceDetected = true;
			semaphore.Release();
		}
	};

	auto first = std::thread(worker);
	auto second = std::thread(worker);
	first.join();
	second.join();

	EXPECT_EQ(counter, 2000);
	EXPECT_FALSE(raceDetected);
}
