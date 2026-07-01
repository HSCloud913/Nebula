#include <gtest/gtest.h>
#include "Timer/TimerWheel.h"
#include <atomic>

using namespace ne::time;

TEST(TimerWheelTest, ScheduleAndFire)
{
    TimerWheel wheel;
    std::atomic<int> count{ 0 };

    wheel.Schedule(std::chrono::milliseconds{ 5 }, [&] { count.fetch_add(1); });

    for (int i = 0; i < 10; ++i) wheel.Tick();

    EXPECT_EQ(count.load(), 1);
}

TEST(TimerWheelTest, CancelBeforeFire)
{
    TimerWheel wheel;
    std::atomic<int> count{ 0 };

    const ne::ulonglong_t id = wheel.Schedule(std::chrono::milliseconds{ 5 }, [&] { count.fetch_add(1); });
    EXPECT_TRUE(wheel.Cancel(id));

    for (int i = 0; i < 10; ++i) wheel.Tick();

    EXPECT_EQ(count.load(), 0);
}

TEST(TimerWheelTest, MultipleTimers)
{
    TimerWheel wheel;
    std::atomic<int> count{ 0 };

    wheel.Schedule(std::chrono::milliseconds{ 2 }, [&] { count.fetch_add(1); });
    wheel.Schedule(std::chrono::milliseconds{ 4 }, [&] { count.fetch_add(1); });
    wheel.Schedule(std::chrono::milliseconds{ 8 }, [&] { count.fetch_add(1); });

    for (int i = 0; i < 10; ++i) wheel.Tick();

    EXPECT_EQ(count.load(), 3);
}
