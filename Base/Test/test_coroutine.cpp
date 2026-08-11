//
// Created by hscloud on 26. 8. 6.
//
// Event / Detached / IExecutor / Race / WhenAny 검증.
// 실제 이벤트 루프(io::Context) 없이도 계약을 확인할 수 있도록, 테스트가 직접 구동하는 최소 실행자를 쓴다.

#include <gtest/gtest.h>
#include <coroutine>
#include <cstddef>
#include <utility>
#include <vector>
#include "Base/Coroutine/Task.h"
#include "Base/Coroutine/IExecutor.h"
#include "Base/Coroutine/Event.h"
#include "Base/Coroutine/Detached.h"
#include "Base/Coroutine/Race.h"
#include "Base/Coroutine/WhenAny.h"

using namespace ne;

namespace
{
	/**
	 * @class ManualExecutor
	 * @brief Post 된 핸들을 큐에 모아 두고 Drain() 시점에만 재개하는 테스트용 실행자.
	 *
	 * io::Context 를 끌어오지 않고 IExecutor 계약만으로 지연 재개(SignalDeferred/WhenAny)를 검증한다.
	 */
	class ManualExecutor final : public IExecutor
	{
	public:
		void_t Post(const std::coroutine_handle<> _handle) override { queue.push_back(_handle); }

		/** @brief 큐에 쌓인 핸들을 모두 재개한다. 재개 중 추가된 것까지 비울 때까지 반복한다. */
		std::size_t Drain()
		{
			std::size_t resumed = 0;
			while (!queue.empty())
			{
				std::vector<std::coroutine_handle<>> batch;
				batch.swap(queue);

				for (const auto handle : batch)
				{
					if (handle && !handle.done())
					{
						handle.resume();
						++resumed;
					}
				}
			}

			return resumed;
		}

		[[nodiscard]] std::size_t Pending() const noexcept { return queue.size(); }

	private:
		std::vector<std::coroutine_handle<>> queue;
	};

	Task<void_t> WaitOn(Event& _event, int_t& _observed)
	{
		co_await _event;
		++_observed;
	}

	Task<int_t> ValueAfter(Event& _event, const int_t _value)
	{
		co_await _event;
		co_return _value;
	}

	Task<int_t> ImmediateValue(const int_t _value) { co_return _value; }

	Detached IncrementDetached(int_t& _counter)
	{
		++_counter;
		co_return;
	}

	// 첫 suspend 지점에서 호출자에게 제어를 돌려주고, 재개는 실행자가 담당한다.
	struct PostSelf
	{
		IExecutor& executor;

		[[nodiscard]] bool await_ready() const noexcept { return false; }
		void await_suspend(const std::coroutine_handle<> _handle) const { executor.Post(_handle); }
		void await_resume() const noexcept {}
	};

	Detached DetachedThroughExecutor(IExecutor& _executor, int_t& _counter)
	{
		co_await PostSelf{ _executor };
		++_counter;
	}
}



// ───────────────────────── Event ─────────────────────────

// co_await 이 먼저 오고 나중에 Signal — 대기자가 그 자리에서 재개된다.
TEST(EventTest, SignalResumesWaiter)
{
	Event event;
	int_t observed = 0;

	auto waiter = WaitOn(event, observed);
	waiter.Resume(); // 대기 지점까지 진행

	EXPECT_FALSE(waiter.IsReady());
	EXPECT_EQ(observed, 0);

	event.Signal();

	EXPECT_TRUE(waiter.IsReady());
	EXPECT_EQ(observed, 1);
}

// Signal 이 co_await 보다 먼저 와도 상태가 기억되어 다음 co_await 가 즉시 통과해야 한다.
TEST(EventTest, SignalBeforeAwaitIsRemembered)
{
	Event event;
	int_t observed = 0;

	event.Signal(); // 대기자가 아직 없다

	auto waiter = WaitOn(event, observed);
	waiter.Resume();

	EXPECT_TRUE(waiter.IsReady()) << "먼저 도착한 Signal 이 기억되지 않아 대기자가 멈췄다";
	EXPECT_EQ(observed, 1);
}

// await_resume 이 신호를 소비하므로, 다음 co_await 는 다시 대기해야 한다(자동 리셋).
TEST(EventTest, SignalIsConsumedByEachAwait)
{
	Event event;
	int_t observed = 0;

	event.Signal();

	auto first = WaitOn(event, observed);
	first.Resume();
	ASSERT_TRUE(first.IsReady());
	ASSERT_EQ(observed, 1);

	// 두 번째 대기자는 새 Signal 없이는 통과하지 못한다.
	auto second = WaitOn(event, observed);
	second.Resume();
	EXPECT_FALSE(second.IsReady());
	EXPECT_EQ(observed, 1);

	event.Signal();
	EXPECT_TRUE(second.IsReady());
	EXPECT_EQ(observed, 2);
}

// SignalDeferred 는 그 자리에서 재개하지 않고 실행자에 예약한다 — 생산자 스택 안에서 소비자가
// 돌지 않게 해(깊은 재진입·생산자 프레임 파괴 방지) 다음 tick 으로 넘기는 것이 목적이다.
TEST(EventTest, SignalDeferredPostponesResumption)
{
	ManualExecutor executor;
	Event event;
	int_t observed = 0;

	auto waiter = WaitOn(event, observed);
	waiter.Resume();
	ASSERT_FALSE(waiter.IsReady());

	event.SignalDeferred(executor);

	// 아직 재개되지 않았다 — 실행자 큐에만 올라가 있다.
	EXPECT_FALSE(waiter.IsReady());
	EXPECT_EQ(observed, 0);
	EXPECT_EQ(executor.Pending(), 1u);

	EXPECT_EQ(executor.Drain(), 1u);

	EXPECT_TRUE(waiter.IsReady());
	EXPECT_EQ(observed, 1);
}

// 대기자가 없을 때의 SignalDeferred 는 상태만 기록하고 아무것도 예약하지 않는다.
TEST(EventTest, SignalDeferredWithoutWaiterOnlyRecordsState)
{
	ManualExecutor executor;
	Event event;
	int_t observed = 0;

	event.SignalDeferred(executor);
	EXPECT_EQ(executor.Pending(), 0u);

	auto waiter = WaitOn(event, observed);
	waiter.Resume();

	EXPECT_TRUE(waiter.IsReady());
	EXPECT_EQ(observed, 1);
}

TEST(EventTest, IsNonCopyableAndNonMovable)
{
	static_assert(!std::is_copy_constructible_v<Event>, "Event 는 복사 불가여야 한다");
	static_assert(!std::is_move_constructible_v<Event>, "Event 는 이동 불가여야 한다(대기자 핸들을 들고 있다)");
}



// ───────────────────────── Detached ─────────────────────────

// initial_suspend = never 이므로 호출 즉시 본문이 돌고, final_suspend = never 이므로 프레임이 자동 파괴된다.
TEST(DetachedTest, RunsImmediatelyWithoutOwner)
{
	int_t counter = 0;

	IncrementDetached(counter); // 반환값을 보관하지 않는다(fire-and-forget)

	EXPECT_EQ(counter, 1);
}

// 첫 suspend 에서 호출자로 제어가 돌아오고, 재개는 실행자가 담당한다.
TEST(DetachedTest, SuspendsAtFirstAwaitAndResumesOnExecutor)
{
	ManualExecutor executor;
	int_t counter = 0;

	DetachedThroughExecutor(executor, counter);

	// 아직 완료되지 않았다 — 대기점에서 호출자에게 돌아왔다.
	EXPECT_EQ(counter, 0);
	EXPECT_EQ(executor.Pending(), 1u);

	executor.Drain();
	EXPECT_EQ(counter, 1);
}



// ───────────────────────── Race (콤비네이터 빌딩 블록) ─────────────────────────

// 아직 결정되지 않았으면 대기하고, 결정된 뒤에는 즉시 통과한다.
TEST(RaceTest, AwaitDecisionPassesOnlyWhenDecided)
{
	RaceState undecided{};
	EXPECT_FALSE(AwaitDecision{ undecided }.await_ready());

	RaceState decided{};
	decided.isDecided = true;
	EXPECT_TRUE(AwaitDecision{ decided }.await_ready());
}

TEST(RaceTest, AwaitSuspendRecordsOuterHandle)
{
	RaceState state{};
	int_t observed = 0;
	Event unused;

	auto outer = WaitOn(unused, observed); // 핸들을 얻기 위한 임의의 코루틴
	outer.Resume();

	// await_suspend 는 자신의 핸들을 state.outer 에 남겨 승자가 깨울 수 있게 한다.
	EXPECT_FALSE(static_cast<bool_t>(state.outer));

	unused.Signal(); // outer 정리
}



// ───────────────────────── WhenAny ─────────────────────────

// 첫 레이서가 동기적으로 완료되면 그 자리에서 승부가 결정되고, 뒤 레이서는 시작조차 하지 않는다.
TEST(WhenAnyTest, SynchronouslyReadyRacerWinsImmediately)
{
	ManualExecutor executor;

	std::vector<Task<int_t>> tasks;
	tasks.push_back(ImmediateValue(10));
	tasks.push_back(ImmediateValue(20));

	auto race = WhenAny(executor, std::move(tasks));
	race.Resume();

	ASSERT_TRUE(race.IsReady());
	const auto result = race.await_resume();
	EXPECT_EQ(result.index, 0u);
	EXPECT_EQ(result.value, 10);
}

// 모두 대기 중이면 승부가 안 나고, 먼저 신호를 받은 쪽이 인덱스와 값을 가져간다.
TEST(WhenAnyTest, FirstCompletedRacerWins)
{
	ManualExecutor executor;
	Event first;
	Event second;

	std::vector<Task<int_t>> tasks;
	tasks.push_back(ValueAfter(first, 111));
	tasks.push_back(ValueAfter(second, 222));

	auto race = WhenAny(executor, std::move(tasks));
	race.Resume();

	ASSERT_FALSE(race.IsReady()); // 둘 다 대기 중

	second.Signal();   // 두 번째가 먼저 끝난다
	executor.Drain();  // 승자가 outer 를 Post 로 깨운다

	ASSERT_TRUE(race.IsReady());
	const auto result = race.await_resume();
	EXPECT_EQ(result.index, 1u);
	EXPECT_EQ(result.value, 222);
}

// 승부가 난 뒤 진 레이서는 파괴로 취소된다 — 나중에 신호가 와도 결과가 바뀌지 않아야 한다.
TEST(WhenAnyTest, LoserIsCancelledByDestruction)
{
	ManualExecutor executor;
	Event winner;
	Event loser;

	std::vector<Task<int_t>> tasks;
	tasks.push_back(ValueAfter(winner, 1));
	tasks.push_back(ValueAfter(loser, 2));

	{
		auto race = WhenAny(executor, std::move(tasks));
		race.Resume();

		winner.Signal();
		executor.Drain();

		ASSERT_TRUE(race.IsReady());
		EXPECT_EQ(race.await_resume().index, 0u);
	} // race 파괴 → 진 레이서(그리고 그가 소유한 Task)도 함께 파괴

	// 진 쪽 Event 를 이제 신호해도 재개할 대기자가 없다(파괴됨) — 크래시 없이 통과해야 한다.
	loser.Signal();
	EXPECT_EQ(executor.Pending(), 0u);
}
