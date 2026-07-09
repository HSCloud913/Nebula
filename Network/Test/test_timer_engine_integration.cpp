#include <gtest/gtest.h>

#if defined(IS_POSIX)

#include <atomic>
#include <chrono>
#include "Io/Engine/Epoll/EpollEngine.h"
#include "Time/Timer/TimerWheel.h"

using namespace ne::time;
using namespace ne::io;

TEST(TimerEngineIntegrationTest, TimerFiresWithoutSocketEvents)
{
    TimerWheel wheel;
    EpollEngine engine;
    ASSERT_TRUE(engine.IsValid());

    engine.SetTimerWheel(&wheel);

    std::atomic<bool> fired{ false };
    wheel.Schedule(std::chrono::milliseconds{ 50 }, [&] { fired.store(true, std::memory_order_release); });

    const auto start = std::chrono::steady_clock::now();

    while (!fired.load(std::memory_order_acquire))
    {
        auto result = engine.RunOnce(-1);
        ASSERT_TRUE(result.IsOk());

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (elapsed > 200)
            break;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_TRUE(fired.load()) << "Timer did not fire within 200ms";
    EXPECT_GE(elapsed, 50) << "Timer fired too early (< 50ms)";
    EXPECT_LE(elapsed, 200) << "Timer fired too late (> 200ms)";
}

TEST(TimerEngineIntegrationTest, NextExpiryMsReturnsNegativeWhenNoTimers)
{
    TimerWheel wheel;
    EXPECT_EQ(wheel.NextExpiryMs(), -1);
}

TEST(TimerEngineIntegrationTest, NextExpiryMsReturnsZeroForOverdueTimer)
{
    // 페이크 클럭으로 Tick 없이 실제 시각만 만료 시점을 지나가게 해 overdue 상태를 만든다.
    std::chrono::steady_clock::time_point now{};
    TimerWheel wheel([&now] { return now; });
    wheel.Schedule(std::chrono::milliseconds{ 1 }, [] {});

    now += std::chrono::milliseconds{ 5 };

    // 만료 시점이 이미 지났으므로 0 반환.
    EXPECT_EQ(wheel.NextExpiryMs(), 0);
}

#elif defined(_WIN32)

#include <atomic>
#include <chrono>
#include "Io/Engine/Iocp/IocpEngine.h"
#include "Time/Timer/TimerWheel.h"

using namespace ne::time;
using namespace ne::io;

TEST(TimerEngineIntegrationTest, TimerFiresWithoutSocketEvents)
{
    TimerWheel wheel;
    IocpEngine engine;
    ASSERT_TRUE(engine.IsValid());

    engine.SetTimerWheel(&wheel);

    std::atomic<bool> fired{ false };
    wheel.Schedule(std::chrono::milliseconds{ 50 }, [&] { fired.store(true, std::memory_order_release); });

    const auto start = std::chrono::steady_clock::now();

    while (!fired.load(std::memory_order_acquire))
    {
        auto result = engine.RunOnce(-1);
        ASSERT_TRUE(result.IsOk());

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (elapsed > 200)
            break;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_TRUE(fired.load()) << "Timer did not fire within 200ms";
    EXPECT_GE(elapsed, 50) << "Timer fired too early (< 50ms)";
    EXPECT_LE(elapsed, 200) << "Timer fired too late (> 200ms)";
}

TEST(TimerEngineIntegrationTest, NextExpiryMsReturnsNegativeWhenNoTimers)
{
    TimerWheel wheel;
    EXPECT_EQ(wheel.NextExpiryMs(), -1);
}

TEST(TimerEngineIntegrationTest, NextExpiryMsReturnsZeroForOverdueTimer)
{
    // 페이크 클럭으로 Tick 없이 실제 시각만 만료 시점을 지나가게 해 overdue 상태를 만든다.
    std::chrono::steady_clock::time_point now{};
    TimerWheel wheel([&now] { return now; });
    wheel.Schedule(std::chrono::milliseconds{ 1 }, [] {});

    now += std::chrono::milliseconds{ 5 };

    EXPECT_EQ(wheel.NextExpiryMs(), 0);
}

#endif
