#include <gtest/gtest.h>
#include "Time/TimerQueue.h"
#include <atomic>
#include <chrono>

using namespace ne::time;

// TimerQueue 은 실시간(steady_clock) 앵커링이 기본이므로, 결정론적 단위 테스트는
// 페이크 클럭을 주입해 시간을 수동으로 전진시킨다. now 를 밀어준 뒤 Tick() 하면
// 그 경과분까지 만료된 타이머가 발화한다.

TEST(TimerQueueTest, ScheduleAndFire)
{
	std::chrono::steady_clock::time_point now{};
	TimerQueue wheel([&now] { return now; });
	std::atomic<int> count{ 0 };

	wheel.Schedule(std::chrono::milliseconds{ 5 }, [&] { count.fetch_add(1); });

	for (int i = 0; i < 10; ++i)
	{
		now += std::chrono::milliseconds{ 1 };
		wheel.Tick();
	}

	EXPECT_EQ(count.load(), 1);
}

TEST(TimerQueueTest, CancelBeforeFire)
{
	std::chrono::steady_clock::time_point now{};
	TimerQueue wheel([&now] { return now; });
	std::atomic<int> count{ 0 };

	const ne::ulonglong_t id = wheel.Schedule(std::chrono::milliseconds{ 5 }, [&] { count.fetch_add(1); });
	EXPECT_TRUE(wheel.Cancel(id));

	for (int i = 0; i < 10; ++i)
	{
		now += std::chrono::milliseconds{ 1 };
		wheel.Tick();
	}

	EXPECT_EQ(count.load(), 0);
}

TEST(TimerQueueTest, MultipleTimers)
{
	std::chrono::steady_clock::time_point now{};
	TimerQueue wheel([&now] { return now; });
	std::atomic<int> count{ 0 };

	wheel.Schedule(std::chrono::milliseconds{ 2 }, [&] { count.fetch_add(1); });
	wheel.Schedule(std::chrono::milliseconds{ 4 }, [&] { count.fetch_add(1); });
	wheel.Schedule(std::chrono::milliseconds{ 8 }, [&] { count.fetch_add(1); });

	for (int i = 0; i < 10; ++i)
	{
		now += std::chrono::milliseconds{ 1 };
		wheel.Tick();
	}

	EXPECT_EQ(count.load(), 3);
}

TEST(TimerQueueTest, CatchUpFiresAcrossMultipleTicksInOneCall)
{
	// wakeup 1 회가 여러 ms 를 블록한 상황 — 단일 Tick() 이 그 경과분을 모두 따라잡아야 한다.
	std::chrono::steady_clock::time_point now{};
	TimerQueue wheel([&now] { return now; });
	std::atomic<int> count{ 0 };

	wheel.Schedule(std::chrono::milliseconds{ 2 }, [&] { count.fetch_add(1); });
	wheel.Schedule(std::chrono::milliseconds{ 5 }, [&] { count.fetch_add(1); });

	now += std::chrono::milliseconds{ 10 };
	wheel.Tick();

	EXPECT_EQ(count.load(), 2);
}

// ── 반복 타이머는 Cancel 까지 매 주기 발화한다 ──
TEST(TimerQueueTest, RepeatingFiresEveryPeriod)
{
	std::chrono::steady_clock::time_point now{};
	TimerQueue queue([&now] { return now; });
	std::atomic<int> count{ 0 };

	const auto id = queue.ScheduleRepeating(std::chrono::milliseconds{ 10 }, [&] { count.fetch_add(1); });
	ASSERT_NE(id, 0u);

	for (int i = 0; i < 5; ++i)
	{
		now += std::chrono::milliseconds{ 10 };
		queue.Tick();
	}
	EXPECT_EQ(count.load(), 5);

	// 취소 후에는 더 이상 발화하지 않는다.
	EXPECT_TRUE(queue.Cancel(id));
	for (int i = 0; i < 5; ++i)
	{
		now += std::chrono::milliseconds{ 10 };
		queue.Tick();
	}
	EXPECT_EQ(count.load(), 5);
}

// ── 여러 주기를 한꺼번에 넘겼어도 밀린 만큼 몰아 실행하지 않는다 ──
//
// 루프가 오래 블록됐을 때 밀린 주기를 모두 발화하면 타이머가 폭주한다. 현재 시각 이후의 첫 경계로
// 건너뛰는 것이 의도된 동작이다.
TEST(TimerQueueTest, RepeatingDoesNotBurstAfterLongStall)
{
	std::chrono::steady_clock::time_point now{};
	TimerQueue queue([&now] { return now; });
	std::atomic<int> count{ 0 };

	(void)queue.ScheduleRepeating(std::chrono::milliseconds{ 10 }, [&] { count.fetch_add(1); });

	now += std::chrono::milliseconds{ 1000 }; // 100 주기 분량을 한 번에 경과
	queue.Tick();

	EXPECT_EQ(count.load(), 1) << "밀린 주기를 몰아 실행하면 안 된다";
}

TEST(TimerQueueTest, RepeatingRejectsNonPositivePeriod)
{
	TimerQueue queue;
	EXPECT_EQ(queue.ScheduleRepeating(std::chrono::milliseconds{ 0 }, [] {}), 0u);
	EXPECT_EQ(queue.ScheduleRepeating(std::chrono::milliseconds{ -5 }, [] {}), 0u);
}

// ── Now() 는 주입된 클럭을 따른다 — Deadline() 계산의 기준이다 ──
//
// 예전 Deadline() 은 steady_clock::now() 를 직접 써서, 페이크 클럭을 주입하면 기준이 어긋나
// (목표시각 - 실제현재) 라는 무의미한 지연을 계산했다.
TEST(TimerQueueTest, NowFollowsInjectedClock)
{
	std::chrono::steady_clock::time_point now{};
	TimerQueue queue([&now] { return now; });

	EXPECT_EQ(queue.Now(), now);

	now += std::chrono::milliseconds{ 1234 };
	EXPECT_EQ(queue.Now(), now);
}
