//
// Created by hscloud on 26. 8. 6.
//
// Task 의 수명·재개 계약 검증. Base 는 헤더 온리(INTERFACE) 타깃이라 이 테스트가 없으면 컴파일
// 검증조차 되지 않는다 — 특히 "중도 파괴가 정상 경로" 라는 계약은 상위 콤비네이터(WhenAny/Timeout)가
// 진 쪽 Task 를 그대로 파괴해 취소하는 데 기대고 있으므로 여기서 고정한다.

#include <gtest/gtest.h>
#include <coroutine>
#include <type_traits>
#include <utility>
#include "Base/Coroutine/Task.h"

using namespace ne;

namespace
{
	Task<int_t> Immediate(const int_t _value) { co_return _value; }

	Task<int_t> SumOfTwo(const int_t _left, const int_t _right)
	{
		const int_t left = co_await Immediate(_left);
		const int_t right = co_await Immediate(_right);

		co_return left + right;
	}

	Task<void_t> SetFlag(bool_t& _isSet)
	{
		_isSet = true;
		co_return;
	}

	// 재개 시점을 테스트가 직접 통제하는 대기점 — 코루틴을 "진행 중" 상태로 붙잡아 둘 때 쓴다.
	struct ManualAwaiter
	{
		std::coroutine_handle<>& slot;

		[[nodiscard]] bool await_ready() const noexcept { return false; }
		void await_suspend(const std::coroutine_handle<> _handle) const noexcept { slot = _handle; }
		void await_resume() const noexcept {}
	};

	// 대기 중 소멸되면 isDestroyed 를 켜는 감시자 — 프레임 파괴 시 지역 객체 소멸자가 도는지 확인한다.
	struct DestructionWitness
	{
		bool_t& isDestroyed;

		~DestructionWitness() { isDestroyed = true; }
	};

	Task<void_t> SuspendForever(std::coroutine_handle<>& _slot, bool_t& _isDestroyed)
	{
		const DestructionWitness witness{ _isDestroyed };

		co_await ManualAwaiter{ _slot };
		co_return;
	}

	// 깊게 중첩된 co_await — symmetric transfer 라면 스택이 깊이에 비례해 자라지 않는다.
	Task<int_t> Countdown(const int_t _depth)
	{
		if (_depth == 0) co_return 0;

		co_return 1 + co_await Countdown(_depth - 1);
	}
}



// ───────────────────────── 기본 재개/결과 ─────────────────────────

TEST(TaskTest, ResumeProducesResult)
{
	auto task = Immediate(42);

	EXPECT_TRUE(task.IsValid());
	EXPECT_FALSE(task.IsReady()); // initial_suspend = suspend_always — Resume 전에는 시작조차 안 한다

	task.Resume();

	EXPECT_TRUE(task.IsReady());
	EXPECT_EQ(task.await_resume(), 42);
}

TEST(TaskTest, VoidSpecializationRuns)
{
	bool_t isSet = false;
	auto task = SetFlag(isSet);

	EXPECT_FALSE(isSet); // 아직 시작 전
	task.Resume();

	EXPECT_TRUE(task.IsReady());
	EXPECT_TRUE(isSet);
}

TEST(TaskTest, AwaitChainsNestedTasks)
{
	auto task = SumOfTwo(20, 22);
	task.Resume();

	ASSERT_TRUE(task.IsReady());
	EXPECT_EQ(task.await_resume(), 42);
}

// 완료된 Task 를 다시 Resume 해도 아무 일도 없어야 한다(handle.done() 가드).
TEST(TaskTest, ResumeAfterCompletionIsNoop)
{
	auto task = Immediate(7);
	task.Resume();
	ASSERT_TRUE(task.IsReady());

	task.Resume();
	task.Resume();

	EXPECT_EQ(task.await_resume(), 7);
}



// ───────────────────────── move-only 계약 ─────────────────────────

TEST(TaskTest, IsMoveOnly)
{
	static_assert(!std::is_copy_constructible_v<Task<int_t>>, "Task 는 복사 불가여야 한다");
	static_assert(!std::is_copy_assignable_v<Task<int_t>>, "Task 는 복사 대입 불가여야 한다");
	static_assert(std::is_move_constructible_v<Task<int_t>>, "Task 는 이동 가능해야 한다");
	static_assert(std::is_move_assignable_v<Task<int_t>>, "Task 는 이동 대입 가능해야 한다");

	auto original = Immediate(11);
	auto moved = std::move(original);

	// 이동된 원본은 handle 을 잃고, 소멸 시 아무것도 파괴하지 않는다(이중 파괴 방지).
	EXPECT_FALSE(original.IsValid());
	EXPECT_TRUE(original.IsReady()); // handle 없음 = 완료로 취급
	EXPECT_TRUE(moved.IsValid());

	moved.Resume();
	EXPECT_EQ(moved.await_resume(), 11);
}

TEST(TaskTest, MoveAssignmentDestroysPreviousFrame)
{
	bool_t isFirstDestroyed = false;
	std::coroutine_handle<> slot{};

	auto task = SuspendForever(slot, isFirstDestroyed);
	task.Resume(); // 대기 지점까지 진행
	ASSERT_FALSE(task.IsReady());
	ASSERT_FALSE(isFirstDestroyed);

	// 다른 Task 를 대입하면 기존 프레임(대기 중)이 파괴되어야 한다.
	bool_t isReplacementSet = false;
	task = SetFlag(isReplacementSet);

	EXPECT_TRUE(isFirstDestroyed) << "대입으로 밀려난 프레임이 파괴되지 않았다";

	task.Resume();
	EXPECT_TRUE(isReplacementSet);
}



// ───────────────────────── 중도 파괴가 정상 경로 ─────────────────────────

// 진행 중(suspend 상태)인 Task 를 그대로 파괴해도 프레임과 지역 객체가 정리되어야 한다.
// WhenAny/Timeout 이 진 쪽을 파괴해 취소하는 방식이 이 계약에 기대고 있다.
TEST(TaskTest, DestroyingSuspendedTaskIsSafe)
{
	bool_t isDestroyed = false;
	std::coroutine_handle<> slot{};

	{
		auto task = SuspendForever(slot, isDestroyed);
		task.Resume();

		ASSERT_FALSE(task.IsReady());           // 대기 중
		ASSERT_FALSE(isDestroyed);
		ASSERT_TRUE(static_cast<bool_t>(slot)); // 대기점이 핸들을 넘겨받았다
	} // 여기서 파괴 — 재개 없이 폐기하는 것이 정상 경로

	EXPECT_TRUE(isDestroyed) << "대기 중 파괴에서 코루틴 지역 객체의 소멸자가 돌지 않았다";
}

// 시작조차 하지 않은(initial_suspend 상태) Task 를 파괴해도 안전해야 한다.
TEST(TaskTest, DestroyingNeverStartedTaskIsSafe)
{
	bool_t isDestroyed = false;
	std::coroutine_handle<> slot{};

	{
		auto task = SuspendForever(slot, isDestroyed);
		ASSERT_FALSE(task.IsReady());
	}

	// Resume 을 한 번도 하지 않았으므로 본문이 실행되지 않았다 — witness 자체가 만들어지지 않는다.
	EXPECT_FALSE(isDestroyed);
	EXPECT_FALSE(static_cast<bool_t>(slot));
}



// ───────────────────────── symmetric transfer ─────────────────────────

// 깊게 중첩해도 스택이 깊이에 비례해 자라지 않아야 한다(FinalAwaiter 가 호출자로 직접 이동).
TEST(TaskTest, DeepAwaitChainDoesNotOverflowStack)
{
	constexpr int_t Depth = 10000;

	auto task = Countdown(Depth);
	task.Resume();

	ASSERT_TRUE(task.IsReady());
	EXPECT_EQ(task.await_resume(), Depth);
}
