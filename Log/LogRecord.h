//
// Created by hscloud on 26. 7. 23.
//

#pragma once
#include <chrono>
#include <ctime>
#include <format>
#include <iomanip>
#include <sstream>
#include "Base/Type.h"

namespace ne::log
{
	enum class LogLevel
	{
		NE_TRACE,
		NE_DEBUG,
		NE_INFO,
		NE_WARNING,
		NE_ERROR,
		NE_FATAL
	};

	struct LogRecord
	{
		LogLevel level{ LogLevel::NE_TRACE };
		string_t message;
		std::chrono::system_clock::time_point timestamp;
	};

	[[nodiscard]] inline string_t LevelToString(const LogLevel _level)
	{
		switch (_level)
		{
			case LogLevel::NE_TRACE:   return "[TRACE]";
			case LogLevel::NE_DEBUG:   return "[DEBUG]";
			case LogLevel::NE_INFO:    return "[INFO]";
			case LogLevel::NE_WARNING: return "[WARNING]";
			case LogLevel::NE_ERROR:   return "[ERROR]";
			case LogLevel::NE_FATAL:   return "[FATAL]";
			default:                   return "";
		}
	}

	[[nodiscard]] inline string_t FormatTimestamp(const std::chrono::system_clock::time_point _timePoint)
	{
		const std::time_t time = std::chrono::system_clock::to_time_t(_timePoint);
		const auto millisecond = std::chrono::duration_cast<std::chrono::milliseconds>(_timePoint.time_since_epoch()) % 1000;

		std::tm tm{};
#if defined(_WIN32)
		localtime_s(&tm, &time);
#else
		localtime_r(&time, &tm);
#endif

		std::ostringstream oss;
		oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << ":" << std::setw(3) << std::setfill('0') << millisecond.count() << "]";

		return oss.str();
	}

	/** @brief 기본 포맷 "[timestamp] [LEVEL] message". 모든 sink 가 공통으로 사용한다. */
	[[nodiscard]] inline string_t FormatRecord(const LogRecord& _record)
	{
		return std::format("{} {} {}", FormatTimestamp(_record.timestamp), LevelToString(_record.level), _record.message);
	}
}
