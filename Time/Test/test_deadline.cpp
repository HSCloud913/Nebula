//
// Created by hscloud on 26. 8. 12.
//

// Deadline(절대 시각 시한) 값 타입 검증.
//
// 상대 지연(duration)만으로 시한을 다루면 계층을 내려갈 때마다 각 단계가 자기 몫의 시간을 새로
// 부여받아 전체 예산이 단계 수만큼 늘어난다. 절대 시각은 그 누적을 구조적으로 막는다.

#include <gtest/gtest.h>

#include <chrono>
#include "Time/Deadline.h"

using namespace ne;
using namespace ne::time;

namespace
{
	// 테스트는 실제 시간에 의존하지 않도록 기준 시각을 직접 만들어 쓴다.
	constexpr Deadline::Clock::time_point Origin{};
}

// ── 기본 생성은 "무기한" 이다 ──
//
// 0(즉시 만료)을 기본으로 두면 초기화를 잊었을 때 요청이 통째로 실패하는데, 그 원인을 찾기가
// 훨씬 어렵다. 무기한이면 최소한 동작은 한다.
TEST(DeadlineTest, DefaultIsIndefinite)
{
	const Deadline deadline;

	EXPECT_FALSE(deadline.HasExpiry());
	EXPECT_FALSE(deadline.IsExpired(Origin));
	EXPECT_FALSE(deadline.IsExpired(Origin + std::chrono::hours(1000)));
	EXPECT_GT(deadline.Remaining(Origin).count(), 0);
}

// ── After() 는 기준 시각으로부터 상대 지연을 절대 시각으로 바꾼다 ──
TEST(DeadlineTest, AfterConvertsDurationToAbsolute)
{
	const auto deadline = Deadline::After(Origin, std::chrono::milliseconds(500));

	ASSERT_TRUE(deadline.HasExpiry());
	EXPECT_EQ(deadline.Remaining(Origin).count(), 500);
	EXPECT_EQ(deadline.Remaining(Origin + std::chrono::milliseconds(200)).count(), 300);
	EXPECT_EQ(deadline.Remaining(Origin + std::chrono::milliseconds(500)).count(), 0);

	EXPECT_FALSE(deadline.IsExpired(Origin + std::chrono::milliseconds(499)));
	EXPECT_TRUE(deadline.IsExpired(Origin + std::chrono::milliseconds(500)));
	EXPECT_TRUE(deadline.IsExpired(Origin + std::chrono::milliseconds(501)));
}

// ── 0 이하의 지연은 무기한으로 본다(설정하지 않은 것과 같게) ──
TEST(DeadlineTest, NonPositiveDurationMeansIndefinite)
{
	EXPECT_FALSE(Deadline::After(Origin, std::chrono::milliseconds(0)).HasExpiry());
	EXPECT_FALSE(Deadline::After(Origin, std::chrono::milliseconds(-100)).HasExpiry());
}

// ── 이미 지난 시한의 남은 시간은 음수가 아니라 0 이다 ──
//
// 음수를 돌려주면 호출자가 타이머에 음수 지연을 넣거나 부호 검사를 잊어 즉시 발화를 놓친다.
TEST(DeadlineTest, RemainingClampsToZero)
{
	const auto deadline = Deadline::After(Origin, std::chrono::milliseconds(100));

	EXPECT_EQ(deadline.Remaining(Origin + std::chrono::seconds(10)).count(), 0);
}

// ── Earliest() 는 상위 예산과 단계 예산 중 더 이른 것을 고른다 ──
//
// 이것이 "예산 공유" 의 핵심이다. 단계가 자기 시한을 원해도 상위 요청 시한을 넘길 수는 없다.
TEST(DeadlineTest, EarliestPicksTighterBudget)
{
	const auto outer = Deadline::After(Origin, std::chrono::milliseconds(1000));
	const auto inner = Deadline::After(Origin, std::chrono::milliseconds(300));

	EXPECT_EQ(outer.Earliest(inner).Remaining(Origin).count(), 300);
	EXPECT_EQ(inner.Earliest(outer).Remaining(Origin).count(), 300) << "순서에 무관해야 한다";
}

// ── 한쪽이 무기한이면 다른 쪽이 이긴다 ──
TEST(DeadlineTest, EarliestIgnoresIndefinite)
{
	const auto bounded = Deadline::After(Origin, std::chrono::milliseconds(250));
	const Deadline indefinite;

	EXPECT_EQ(bounded.Earliest(indefinite).Remaining(Origin).count(), 250);
	EXPECT_EQ(indefinite.Earliest(bounded).Remaining(Origin).count(), 250);
	EXPECT_FALSE(indefinite.Earliest(Deadline{}).HasExpiry());
}

// ── 같은 Deadline 을 여러 단계에 넘기면 예산이 누적되지 않는다 ──
//
// 이 테스트가 이 타입의 존재 이유다. 상대 지연이면 세 단계에 각 100ms 를 주면 총 300ms 를 쓸 수
// 있지만, 하나의 Deadline 을 공유하면 세 단계를 합쳐 100ms 안에 끝나야 한다.
TEST(DeadlineTest, SharedDeadlineDoesNotAccumulateBudget)
{
	const auto shared = Deadline::After(Origin, std::chrono::milliseconds(100));

	auto now = Origin;

	// 1단계: 40ms 소비
	now += std::chrono::milliseconds(40);
	EXPECT_EQ(shared.Remaining(now).count(), 60);
	EXPECT_FALSE(shared.IsExpired(now));

	// 2단계: 40ms 더 소비
	now += std::chrono::milliseconds(40);
	EXPECT_EQ(shared.Remaining(now).count(), 20);
	EXPECT_FALSE(shared.IsExpired(now));

	// 3단계: 예산 초과
	now += std::chrono::milliseconds(40);
	EXPECT_EQ(shared.Remaining(now).count(), 0);
	EXPECT_TRUE(shared.IsExpired(now)) << "세 단계 합계가 예산을 넘겼는데 만료되지 않았다";
}
