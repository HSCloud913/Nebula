//
// Created by hscloud on 26. 8. 11.
//

#include "Network/Protocol/Http/Message/Date.h"

#include <array>
#include <charconv>
#include <cstdio>

namespace ne::network::http
{
	namespace
	{
		constexpr std::array<string_view_t, 7> DayNames = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
		constexpr std::array<string_view_t, 12> MonthNames = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

		// IMF-fixdate 는 정확히 이 길이다: "Sun, 06 Nov 1994 08:49:37 GMT"
		constexpr std::size_t FixdateLength = 29;

		[[nodiscard]] bool_t IsLeapYear(const int_t _year) noexcept { return (_year % 4 == 0 && _year % 100 != 0) || _year % 400 == 0; }

		[[nodiscard]] int_t DaysInMonth(const int_t _year, const int_t _month) noexcept
		{
			constexpr std::array<int_t, 12> days = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
			if (_month == 2 && IsLeapYear(_year)) return 29;

			return days[static_cast<std::size_t>(_month - 1)];
		}

		// _text 의 [_offset, _offset+_length) 를 10진 정수로 읽는다(자릿수가 정확히 맞아야 성공).
		[[nodiscard]] bool_t ReadFixedDigits(const string_view_t _text, const std::size_t _offset, const std::size_t _length, int_t& _out) noexcept
		{
			const char* first = _text.data() + _offset;
			const char* last = first + _length;
			const auto [ptr, ec] = std::from_chars(first, last, _out);

			return ec == std::errc{} && ptr == last;
		}

		[[nodiscard]] std::optional<int_t> IndexOf(const string_view_t _needle, const auto& _table) noexcept
		{
			for (std::size_t i = 0; i < _table.size(); ++i) if (_table[i] == _needle) return static_cast<int_t>(i);

			return std::nullopt;
		}
	}



	string_t FormatDate(const std::chrono::system_clock::time_point _timePoint)
	{
		using namespace std::chrono;

		const auto days_ = floor<days>(_timePoint);
		const year_month_day date{ days_ };
		const hh_mm_ss time{ floor<seconds>(_timePoint - days_) };
		const weekday day{ days_ };

		// IMF-fixdate 는 4자리 연도만 표현한다. system_clock 은 서기 31000년 대까지 표현할 수 있어
		// time_point::max() 같은 값을 넣으면 5자리가 되고, 고정 폭 버퍼에서 조용히 잘려 무효한 Date
		// 헤더가 만들어진다. 표현 불가한 값은 형식을 깨는 대신 빈 문자열로 알린다.
		const int_t year = static_cast<int_t>(date.year());
		if (year < 1 || year > 9999) return string_t{};

		// 표 조회 + 고정 폭 포맷 — strftime 은 로케일에 따라 요일/월 이름이 바뀌어 쓸 수 없다.
		char buffer[FixdateLength + 1]{};
		(void)std::snprintf(buffer,
							sizeof(buffer),
							"%.3s, %02d %.3s %04d %02d:%02d:%02d GMT",
							DayNames[day.c_encoding()].data(),
							static_cast<int>(static_cast<unsigned>(date.day())),
							MonthNames[static_cast<unsigned>(date.month()) - 1].data(),
							static_cast<int>(year),
							static_cast<int>(time.hours().count()),
							static_cast<int>(time.minutes().count()),
							static_cast<int>(time.seconds().count()));

		return string_t(buffer);
	}

	std::optional<std::chrono::system_clock::time_point> ParseDate(const string_view_t _text)
	{
		using namespace std::chrono;

		if (_text.size() != FixdateLength) return std::nullopt;

		// 고정 위치 검증: "Sun, 06 Nov 1994 08:49:37 GMT"
		//                 0123456789...
		if (_text[3] != ',' || _text[4] != ' ' || _text[7] != ' ' || _text[11] != ' ' || _text[16] != ' ' || _text[19] != ':' || _text[22] != ':' || _text.substr(25) != " GMT") return std::nullopt;

		// 요일은 검증만 하고 날짜 계산에는 쓰지 않는다(날짜와 모순되더라도 날짜를 신뢰한다).
		if (!IndexOf(_text.substr(0, 3), DayNames)) return std::nullopt;

		const auto monthIndex = IndexOf(_text.substr(8, 3), MonthNames);
		if (!monthIndex) return std::nullopt;

		int_t day = 0, year = 0, hour = 0, minute = 0, second = 0;
		if (!ReadFixedDigits(_text, 5, 2, day) || !ReadFixedDigits(_text, 12, 4, year) || !ReadFixedDigits(_text, 17, 2, hour) || !ReadFixedDigits(_text, 20, 2, minute) || !ReadFixedDigits(_text, 23, 2, second)) return std::nullopt;

		// from_chars 는 선행 "-" 를 받아들이므로 음수 검사가 **DaysInMonth 호출보다 먼저** 와야 한다
		// ("Sun, 06 Nov -123 08:49:37 GMT" 가 통과했고, 음수 연도로 윤년 계산까지 했다).
		if (day < 1 || year < 1 || hour < 0 || minute < 0 || second < 0) return std::nullopt;

		const int_t month = *monthIndex + 1;
		if (day > DaysInMonth(year, month)) return std::nullopt;

		// 초 60 은 윤초 표기다 — 거부하지 않고 59 로 클램프한다(system_clock 에 윤초가 없다).
		if (hour > 23 || minute > 59 || second > 60) return std::nullopt;
		if (second == 60) second = 59;

		const year_month_day date{ std::chrono::year{ year }, std::chrono::month{ static_cast<unsigned>(month) }, std::chrono::day{ static_cast<unsigned>(day) } };
		if (!date.ok()) return std::nullopt;

		return sys_days{ date } + hours{ hour } + minutes{ minute } + seconds{ second };
	}
}
