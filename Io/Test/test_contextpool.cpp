#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <functional>
#include <thread>
#include <unordered_map>
#include "Io/Context/ContextPool.h"
#include "Io/Context/Context.h"

using namespace ne;
using namespace ne::io;

namespace
{
	// fire-and-forget 코루틴 — 본문 실행 후 final_suspend(suspend_never)에서 프레임을 스스로 파괴한다.
	// 워커 스레드로 Post 되어 그 위에서 구동되는지만 확인하는 용도라 결과값을 들고 있을 필요가 없다.
	struct Detached
	{
		struct promise_type
		{
			Detached get_return_object() { return Detached{ std::coroutine_handle<promise_type>::from_promise(*this) }; }
			std::suspend_always initial_suspend() noexcept { return {}; }
			std::suspend_never final_suspend() noexcept { return {}; }
			void_t return_void() noexcept {}
			void_t unhandled_exception() noexcept { std::terminate(); }
		};

		std::coroutine_handle<promise_type> handle;
	};

	Detached Work(std::atomic<int_t>& _counter, std::atomic<std::size_t>& _tid)
	{
		_tid.store(std::hash<std::thread::id>{}(std::this_thread::get_id()), std::memory_order_relaxed);
		_counter.fetch_add(1, std::memory_order_release);
		co_return;
	}
}

// ── 라이프사이클: 정상 Start/Stop, 즉시 start→stop 경합, 소멸자 전용 정지가 hang/terminate 없이 끝난다 ──
TEST(ContextPoolTest, LifecycleIsClean)
{
	for (int_t i = 0; i < 50; ++i)
	{
		ContextPool pool{ 4 };
		EXPECT_EQ(pool.Size(), 4u);
		EXPECT_FALSE(pool.IsRunning());
		pool.Start();
		EXPECT_TRUE(pool.IsRunning());
		pool.Stop();
		EXPECT_FALSE(pool.IsRunning());
	}

	// 스폰 직후 곧바로 Stop — 워커가 아직 Run() 에 진입 전일 수 있는 경합(Context 가 isStopRequested 로 흡수).
	for (int_t i = 0; i < 50; ++i)
	{
		ContextPool pool{ 4 };
		pool.Start();
		pool.Stop();
	}

	// 명시적 Stop 없이 소멸자만으로 정지/join.
	for (int_t i = 0; i < 25; ++i)
	{
		ContextPool pool{ 3 };
		pool.Start();
	}

	SUCCEED();
}

// ── 기본 크기: 0 이면 최소 1 이상(hardware_concurrency) ──
TEST(ContextPoolTest, DefaultSizeIsPositive)
{
	ContextPool pool{ 0 };
	EXPECT_GE(pool.Size(), 1u);
}

// ── Acquire 는 모든 워커를 round-robin 으로 고르게 배정한다(스레드 불필요) ──
TEST(ContextPoolTest, AcquireRoundRobin)
{
	ContextPool pool{ 4 };

	std::unordered_map<Context*, int_t> hits;
	for (int_t i = 0; i < 4000; ++i) hits[&pool.Acquire()]++;

	EXPECT_EQ(hits.size(), 4u);
	for (const auto& [context, count] : hits)
		EXPECT_EQ(count, 1000);
}

// ── 워커 스레드가 실제로 Run() 을 돌며, 타 스레드에서 Post 된 코루틴을 워커 위에서 재개한다 ──
TEST(ContextPoolTest, WorkerThreadsRunPostedCoroutines)
{
	const std::size_t mainTid = std::hash<std::thread::id>{}(std::this_thread::get_id());

	constexpr int_t workerCount = 4;
	constexpr int_t perWorker = 25;
	constexpr int_t total = workerCount * perWorker;

	ContextPool pool{ static_cast<std::size_t>(workerCount) };
	pool.Start();

	std::atomic<int_t> counter{ 0 };
	std::atomic<std::size_t> workerTids[total];
	Detached jobs[total];
	for (int_t i = 0; i < total; ++i)
	{
		jobs[i] = Work(counter, workerTids[i]);
		pool.At(i % workerCount).Post(jobs[i].handle); // 메인 스레드 → 워커 Context 로 cross-thread 제출(Wake→DrainPosted)
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (counter.load(std::memory_order_acquire) < total && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));

	EXPECT_EQ(counter.load(), total);
	for (int_t i = 0; i < total; ++i)
		EXPECT_NE(workerTids[i].load(std::memory_order_relaxed), mainTid); // 메인이 아니라 워커에서 실행됐다

	pool.Stop();
}
