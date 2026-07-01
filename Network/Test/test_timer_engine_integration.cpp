#include <gtest/gtest.h>

#if defined(IS_POSIX)

#include <atomic>
#include <chrono>
#include "Engine/Epoll/EpollEngine.h"
#include "TimerWheel.h"

using namespace ne::time;
using namespace ne::io;

TEST(TimerEngineIntegrationTest, TimerFiresWithoutSocketEvents)
{
    TimerWheel wheel;
    EpollEngine engine;
    ASSERT_TRUE(engine.IsValid());

    engine.SetTimerWheel(&wheel);

    std::atomic<bool> fired{ false };
    wheel.Schedule(Duration{ 50 }, [&] { fired.store(true, std::memory_order_release); });

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
    TimerWheel wheel;
    wheel.Schedule(Duration{ 1 }, [] {});

    // 충분히 Tick을 돌려 타이머가 과거가 되도록.
    for (int i = 0; i < 10; ++i) wheel.Tick();

    // 이미 만료됐으므로 0 반환.
    EXPECT_EQ(wheel.NextExpiryMs(), 0);
}

#elif defined(_WIN32)

#include <atomic>
#include <chrono>
#include "Engine/Iocp/IocpEngine.h"
#include "TimerWheel.h"

using namespace ne::time;
using namespace ne::io;

TEST(TimerEngineIntegrationTest, TimerFiresWithoutSocketEvents)
{
    TimerWheel wheel;
    IocpEngine engine;
    ASSERT_TRUE(engine.IsValid());

    engine.SetTimerWheel(&wheel);

    std::atomic<bool> fired{ false };
    wheel.Schedule(Duration{ 50 }, [&] { fired.store(true, std::memory_order_release); });

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
    TimerWheel wheel;
    wheel.Schedule(Duration{ 1 }, [] {});

    for (int i = 0; i < 10; ++i) wheel.Tick();

    EXPECT_EQ(wheel.NextExpiryMs(), 0);
}

#endif
