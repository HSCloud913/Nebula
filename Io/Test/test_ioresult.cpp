#include <gtest/gtest.h>

#include "Base/Coroutine/Task.h"
#include "Io/IoResult.h"

using namespace ne;
using namespace ne::io;

namespace
{
	IoResult<int_t> Halve(const int_t _value)
	{
		if (_value % 2 != 0) return IoResult<int_t>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "odd" });
		return IoResult<int_t>::Ok(_value / 2);
	}

	// CO_TRY 로 두 단계 연쇄 — 어느 단계든 에러면 즉시 전파된다.
	ne::Task<IoResult<int_t>> HalveTwice(const int_t _value)
	{
		CO_TRY(first, Halve(_value));
		CO_TRY(second, Halve(first));
		co_return IoResult<int_t>::Ok(second);
	}

	// void 결과 전파.
	ne::Task<IoResult<void_t>> RequireEven(const int_t _value)
	{
		CO_TRYV(Halve(_value));
		co_return IoResult<void_t>::Ok();
	}

	template <typename T>
	T RunSync(ne::Task<T> _task)
	{
		_task.Resume(); // Halve 는 동기 — suspend 없이 완료된다
		return _task.await_resume();
	}
}

TEST(IoResultTest, CoTryPropagatesSuccess)
{
	auto result = RunSync(HalveTwice(20)); // 20 → 10 → 5
	ASSERT_TRUE(result.IsOk());
	EXPECT_EQ(result.Value(), 5);
}

TEST(IoResultTest, CoTryPropagatesErrorAtFirstStep)
{
	auto result = RunSync(HalveTwice(7)); // 7 홀수 → 첫 CO_TRY 에서 전파
	ASSERT_TRUE(result.IsError());
	EXPECT_EQ(result.Error().Kind(), IoErrorKind::INVALID_BUFFER);
}

TEST(IoResultTest, CoTryPropagatesErrorMidChain)
{
	auto result = RunSync(HalveTwice(10)); // 10 → 5(ok), Halve(5) 홀수 → 둘째 CO_TRY 에서 전파
	ASSERT_TRUE(result.IsError());
}

TEST(IoResultTest, CoTryVoidPropagates)
{
	EXPECT_TRUE(RunSync(RequireEven(4)).IsOk());
	EXPECT_TRUE(RunSync(RequireEven(3)).IsError());
}
