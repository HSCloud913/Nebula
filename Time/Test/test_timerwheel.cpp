#include <gtest/gtest.h>
#include "Time/Timer/TimerWheel.h"
#include <atomic>
#include <chrono>

using namespace ne::time;

// TimerWheel 은 실시간(steady_clock) 앵커링이 기본이므로, 결정론적 단위 테스트는
// 페이크 클럭을 주입해 시간을 수동으로 전진시킨다. now 를 밀어준 뒤 Tick() 하면
// 그 경과분까지 만료된 타이머가 발화한다.

TEST(TimerWheelTest, ScheduleAndFire)
{
    std::chrono::steady_clock::time_point now{};
    TimerWheel wheel([&now] { return now; });
    std::atomic<int> count{ 0 };

    wheel.Schedule(std::chrono::milliseconds{ 5 }, [&] { count.fetch_add(1); });

    for (int i = 0; i < 10; ++i)
    {
        now += std::chrono::milliseconds{ 1 };
        wheel.Tick();
    }

    EXPECT_EQ(count.load(), 1);
}

TEST(TimerWheelTest, CancelBeforeFire)
{
    std::chrono::steady_clock::time_point now{};
    TimerWheel wheel([&now] { return now; });
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

TEST(TimerWheelTest, MultipleTimers)
{
    std::chrono::steady_clock::time_point now{};
    TimerWheel wheel([&now] { return now; });
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

TEST(TimerWheelTest, CatchUpFiresAcrossMultipleTicksInOneCall)
{
    // wakeup 1 회가 여러 ms 를 블록한 상황 — 단일 Tick() 이 그 경과분을 모두 따라잡아야 한다.
    std::chrono::steady_clock::time_point now{};
    TimerWheel wheel([&now] { return now; });
    std::atomic<int> count{ 0 };

    wheel.Schedule(std::chrono::milliseconds{ 2 }, [&] { count.fetch_add(1); });
    wheel.Schedule(std::chrono::milliseconds{ 5 }, [&] { count.fetch_add(1); });

    now += std::chrono::milliseconds{ 10 };
    wheel.Tick();

    EXPECT_EQ(count.load(), 2);
}
