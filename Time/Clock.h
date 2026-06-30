//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <chrono>
#include "Type.h"

BEGIN_NS(ne::time)
	using TimePoint = std::chrono::steady_clock::time_point;
	using Duration  = std::chrono::milliseconds;

	[[nodiscard]] inline TimePoint Now() noexcept
	{
		return std::chrono::steady_clock::now();
	}

	[[nodiscard]] inline TimePoint Deadline(Duration _from_now) noexcept
	{
		return Now() + _from_now;
	}
END_NS
