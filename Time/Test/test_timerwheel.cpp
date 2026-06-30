#include <gtest/gtest.h>
#include "TimerWheel.h"
#include <atomic>

using namespace ne::time;

TEST(TimerWheelTest, ScheduleAndFire)
{
    TimerWheel wheel;
    std::atomic<int> count{ 0 };

    wheel.Schedule(Duration{ 5 }, [&] { count.fetch_add(1); });

    for (int i = 0; i < 10; ++i) wheel.Tick();

    EXPECT_EQ(count.load(), 1);
}

TEST(TimerWheelTest, CancelBeforeFire)
{
    TimerWheel wheel;
    std::atomic<int> count{ 0 };

    const TimerId id = wheel.Schedule(Duration{ 5 }, [&] { count.fetch_add(1); });
    EXPECT_TRUE(wheel.Cancel(id));

    for (int i = 0; i < 10; ++i) wheel.Tick();

    EXPECT_EQ(count.load(), 0);
}

TEST(TimerWheelTest, MultipleTimers)
{
    TimerWheel wheel;
    std::atomic<int> count{ 0 };

    wheel.Schedule(Duration{ 2 }, [&] { count.fetch_add(1); });
    wheel.Schedule(Duration{ 4 }, [&] { count.fetch_add(1); });
    wheel.Schedule(Duration{ 8 }, [&] { count.fetch_add(1); });

    for (int i = 0; i < 10; ++i) wheel.Tick();

    EXPECT_EQ(count.load(), 3);
}
