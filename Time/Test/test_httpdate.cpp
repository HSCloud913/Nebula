//
// Created by hscloud on 26. 8. 11.
//

// HTTP-date(IMF-fixdate, RFC 9110 §5.6.7) 포맷/파싱 검증.

#include <gtest/gtest.h>

#include <chrono>
#include "Time/HttpDate.h"

using namespace ne;
using namespace ne::time;

namespace
{
	// 연/월/일 시:분:초(UTC)를 system_clock 시각으로 만든다.
	std::chrono::system_clock::time_point Utc(const int _year, const unsigned _month, const unsigned _day, const int _hour, const int _minute, const int _second)
	{
		using namespace std::chrono;
		return sys_days{ year{ _year } / month{ _month } / day{ _day } } + hours{ _hour } + minutes{ _minute } + seconds{ _second };
	}
}

// ── RFC 9110 이 예시로 드는 시각을 정확히 재현한다 ──
TEST(HttpDateTest, FormatsRfcExample)
{
	EXPECT_EQ(FormatHttpDate(Utc(1994, 11, 6, 8, 49, 37)), "Sun, 06 Nov 1994 08:49:37 GMT");
}

TEST(HttpDateTest, FormatsEpoch)
{
	EXPECT_EQ(FormatHttpDate(Utc(1970, 1, 1, 0, 0, 0)), "Thu, 01 Jan 1970 00:00:00 GMT");
}

// ── 포맷 → 파싱 왕복이 원래 시각과 일치한다(초 단위 해상도) ──
TEST(HttpDateTest, RoundTripsThroughParse)
{
	const auto values = { Utc(1970, 1, 1, 0, 0, 0), Utc(1994, 11, 6, 8, 49, 37), Utc(2000, 2, 29, 23, 59, 59), Utc(2026, 8, 11, 12, 34, 56), Utc(2100, 3, 1, 0, 0, 1) };

	for (const auto& value : values)
	{
		const string_t text = FormatHttpDate(value);
		const auto parsed = ParseHttpDate(text);

		ASSERT_TRUE(parsed.has_value()) << "파싱 실패: " << text;
		EXPECT_EQ(*parsed, value) << text;
	}
}

TEST(HttpDateTest, ParsesRfcExample)
{
	const auto parsed = ParseHttpDate("Sun, 06 Nov 1994 08:49:37 GMT");
	ASSERT_TRUE(parsed.has_value());
	EXPECT_EQ(*parsed, Utc(1994, 11, 6, 8, 49, 37));
}

// ── 요일이 날짜와 어긋나도 날짜를 신뢰한다(요일은 중복 정보다) ──
TEST(HttpDateTest, IgnoresInconsistentWeekday)
{
	const auto parsed = ParseHttpDate("Mon, 06 Nov 1994 08:49:37 GMT"); // 실제로는 Sunday
	ASSERT_TRUE(parsed.has_value());
	EXPECT_EQ(*parsed, Utc(1994, 11, 6, 8, 49, 37));
}

// ── 형식을 벗어난 입력은 거부한다 ──
TEST(HttpDateTest, RejectsMalformedInput)
{
	EXPECT_FALSE(ParseHttpDate("").has_value());
	EXPECT_FALSE(ParseHttpDate("Sun, 06 Nov 1994 08:49:37").has_value());        // GMT 누락
	EXPECT_FALSE(ParseHttpDate("Sun, 06 Nov 1994 08:49:37 UTC").has_value());    // GMT 가 아님
	EXPECT_FALSE(ParseHttpDate("Sun  06 Nov 1994 08:49:37 GMT").has_value());    // 쉼표 누락
	EXPECT_FALSE(ParseHttpDate("Xxx, 06 Nov 1994 08:49:37 GMT").has_value());    // 요일 이름 오류
	EXPECT_FALSE(ParseHttpDate("Sun, 06 Xxx 1994 08:49:37 GMT").has_value());    // 월 이름 오류
	EXPECT_FALSE(ParseHttpDate("Sun, 6 Nov 1994 08:49:37 GMT").has_value());     // 일이 2자리가 아님
	EXPECT_FALSE(ParseHttpDate("Sun, 06 Nov 1994 24:49:37 GMT").has_value());    // 시 범위 초과
	EXPECT_FALSE(ParseHttpDate("Sun, 06 Nov 1994 08:60:37 GMT").has_value());    // 분 범위 초과
	EXPECT_FALSE(ParseHttpDate("Wed, 31 Feb 1994 08:49:37 GMT").has_value());    // 존재하지 않는 날짜
	EXPECT_FALSE(ParseHttpDate("Sun, 29 Feb 1900 08:49:37 GMT").has_value());    // 1900 은 윤년이 아니다
	EXPECT_FALSE(ParseHttpDate("Sun, 0a Nov 1994 08:49:37 GMT").has_value());    // 숫자가 아닌 문자
}

// ── 윤초 표기(초 60)는 59 로 클램프해 받아들인다(system_clock 에는 윤초가 없다) ──
TEST(HttpDateTest, ClampsLeapSecond)
{
	const auto parsed = ParseHttpDate("Sun, 31 Dec 1995 23:59:60 GMT");
	ASSERT_TRUE(parsed.has_value());
	EXPECT_EQ(*parsed, Utc(1995, 12, 31, 23, 59, 59));
}

// ── 윤년 2월 29일을 정상 처리한다 ──
TEST(HttpDateTest, HandlesLeapDay)
{
	EXPECT_TRUE(ParseHttpDate("Tue, 29 Feb 2000 12:00:00 GMT").has_value()); // 400 배수 → 윤년
	EXPECT_TRUE(ParseHttpDate("Mon, 29 Feb 2016 12:00:00 GMT").has_value());
	EXPECT_FALSE(ParseHttpDate("Sun, 29 Feb 2100 12:00:00 GMT").has_value()); // 100 배수, 400 배수 아님
}
