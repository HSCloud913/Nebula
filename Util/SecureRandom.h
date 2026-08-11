//
// Created by hscloud on 26. 7. 9.
//

#pragma once
#include <cstddef>
#include <limits>
#include "Base/Type.h"

namespace ne::util
{
	/**
	 * @class SecureRandom
	 * @brief CSPRNG 래퍼입니다. mt19937 등 비암호 PRNG 대신 키/소수/패딩 생성에 사용합니다.
	 *
	 * Windows에서는 BCryptGenRandom(BCRYPT_USE_SYSTEM_PREFERRED_RNG, WinCNG 백엔드)
	 * POSIX에서는 getrandom(2)(실패 시 /dev/urandom 폴백)를 사용합니다.
	 * UniformRandomBitGenerator를 모델링하므로 std::uniform_int_distribution 등과 그대로 조합해 쓸 수 있습니다.
	 */
	class SecureRandom
	{
	public:
		using result_type = ulonglong_t; // UniformRandomBitGenerator 계약의 필수 멤버

	public:
		[[nodiscard]] result_type operator()() noexcept { return Next(); }

	public:
		/**
		 * @brief _length 바이트를 암호학적 난수로 채웁니다.
		 * @return 성공 시 true. OS CSPRNG 호출이 실패하면 false(무음 저엔트로피 방지) — 직접 호출 시 반드시 확인할 것.
		 * @note Next()/operator()는 URBG 계약상 실패를 알릴 수 없으므로, CSPRNG 실패 시 fail-closed(abort)한다.
		 *       예측 가능한 키/난수를 조용히 내보내느니 즉시 중단하는 편이 안전하기 때문이다.
		 */
		[[nodiscard]] bool_t Fill(void_t* _buffer, std::size_t _length) noexcept;

		[[nodiscard]] ulonglong_t Next() noexcept;

	public:
		[[nodiscard]] static constexpr result_type min() noexcept { return 0; }
		[[nodiscard]] static constexpr result_type max() noexcept { return (std::numeric_limits<result_type>::max)(); }
	};
}
