//
// Created by hscloud on 26. 7. 23.
//

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include "Base/Result.h"

using ne::Result;

// Map: Ok 면 값 변환, Error 면 전파
TEST(ResultCombinatorTest, Map)
{
	auto ok = Result<int>::Ok(21).Map([](const int _v) { return _v * 2; });
	ASSERT_TRUE(ok.IsOk());
	EXPECT_EQ(ok.Value(), 42);

	auto err = Result<int>::Error(ne::Error{ "boom" }).Map([](const int _v) { return _v * 2; });
	ASSERT_TRUE(err.IsError());
	EXPECT_EQ(err.Error().Message(), "boom");
}

// Map: F 가 void 를 반환하면 Result<void_t> 로 접힌다
TEST(ResultCombinatorTest, MapToVoid)
{
	int sideEffect = 0;
	ne::Result<ne::void_t> r = Result<int>::Ok(5).Map([&](const int _v) { sideEffect = _v; });
	EXPECT_TRUE(r.IsOk());
	EXPECT_EQ(sideEffect, 5);
}

// AndThen: 실패 가능 연산 체이닝
TEST(ResultCombinatorTest, AndThen)
{
	auto half = [](const int _v) -> Result<int>
	{
		if (_v % 2 != 0) return Result<int>::Error(ne::Error{ "odd" });
		return Result<int>::Ok(_v / 2);
	};

	EXPECT_EQ(Result<int>::Ok(8).AndThen(half).AndThen(half).Value(), 2);
	EXPECT_TRUE(Result<int>::Ok(3).AndThen(half).IsError());                    // 중간 실패 전파
	EXPECT_TRUE(Result<int>::Error(ne::Error{ "x" }).AndThen(half).IsError());  // 시작부터 실패
}

// OrElse: Error 복구 / Ok 통과
TEST(ResultCombinatorTest, OrElse)
{
	auto recover = [](const ne::Error&) { return Result<int>::Ok(-1); };
	EXPECT_EQ(Result<int>::Error(ne::Error{ "e" }).OrElse(recover).Value(), -1);
	EXPECT_EQ(Result<int>::Ok(9).OrElse(recover).Value(), 9);
}

// ValueOr: Ok→값, Error→기본값
TEST(ResultCombinatorTest, ValueOr)
{
	EXPECT_EQ(Result<int>::Ok(7).ValueOr(99), 7);
	EXPECT_EQ(Result<int>::Error(ne::Error{ "e" }).ValueOr(99), 99);
}

// Context: Error 에 컨텍스트 누적(Ok 는 무영향)
TEST(ResultCombinatorTest, Context)
{
	auto r = Result<int>::Error(ne::Error{ "root" }).Context("outer");
	ASSERT_TRUE(r.IsError());
	EXPECT_NE(r.Error().What().find("outer"), std::string::npos);
	EXPECT_NE(r.Error().What().find("root"), std::string::npos);

	EXPECT_TRUE(Result<int>::Ok(1).Context("x").IsOk());
}

// move-only 값도 조합자에서 이동으로 흐른다(복사 불필요)
TEST(ResultCombinatorTest, MoveOnlyValue)
{
	auto r = Result<std::unique_ptr<int>>::Ok(std::make_unique<int>(7)).Map([](const std::unique_ptr<int> _p) { return *_p; });
	ASSERT_TRUE(r.IsOk());
	EXPECT_EQ(r.Value(), 7);
}

// Result<void_t> 조합자 (F 는 인자 없음)
TEST(ResultCombinatorTest, VoidResult)
{
	using RV = ne::Result<ne::void_t>;

	auto mapped = RV::Ok().Map([] { return 5; });
	ASSERT_TRUE(mapped.IsOk());
	EXPECT_EQ(mapped.Value(), 5);

	auto step = [] { return RV::Ok(); };
	EXPECT_TRUE(RV::Ok().AndThen(step).IsOk());
	EXPECT_TRUE(RV::Error(ne::Error{ "e" }).AndThen(step).IsError());

	EXPECT_TRUE(RV::Error(ne::Error{ "e" }).OrElse([](const ne::Error&) { return RV::Ok(); }).IsOk());
}
