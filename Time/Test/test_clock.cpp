#include <gtest/gtest.h>
#include "Clock.h"

using namespace ne::time;

TEST(ClockTest, NowIsMonotonic)
{
    const auto t1 = Now();
    const auto t2 = Now();
    EXPECT_LE(t1, t2);
}

TEST(ClockTest, DeadlineIsInFuture)
{
    const auto before = Now();
    const auto dl = Deadline(Duration{ 100 });
    EXPECT_GT(dl, before);
}
