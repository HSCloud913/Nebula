//
// Created by hscloud on 26. 7. 23.
//

#pragma once
#include <cstddef>
#include <string_view>
#include "Base/Type.h"

namespace ne::crypto::internal
{
	/**
	 * @brief 두 버퍼를 상수시간으로 비교합니다(길이가 같다고 가정).
	 *
	 * MAC/인증 태그 등 비밀 값을 비교할 때 조기 반환(early-exit)으로 생기는 타이밍 사이드채널을
	 * 방지한다. 일반 `memcmp`/`==` 는 첫 불일치에서 즉시 반환해 비교 시간이 값에 의존한다.
	 */
	[[nodiscard]] inline bool_t ConstantTimeEquals(const void_t* _a, const void_t* _b, const std::size_t _length) noexcept
	{
		const auto* a = static_cast<const byte_t*>(_a);
		const auto* b = static_cast<const byte_t*>(_b);

		byte_t diff = 0;
		for (std::size_t i = 0; i < _length; ++i) diff = static_cast<byte_t>(diff | (a[i] ^ b[i]));

		return diff == 0;
	}

	/** @brief 길이가 다르면 즉시 false(길이는 비밀이 아니라고 가정), 같으면 상수시간 비교합니다. */
	[[nodiscard]] inline bool_t ConstantTimeEquals(const string_view_t _a, const string_view_t _b) noexcept
	{
		if (_a.size() != _b.size()) return false;

		return ConstantTimeEquals(_a.data(), _b.data(), _a.size());
	}
}
