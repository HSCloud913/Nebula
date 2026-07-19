//
// Created by hscloud on 26. 7. 9.
//

#pragma once
#include <cstddef>
#include <limits>
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
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
		[[nodiscard]] ulonglong_t operator()() noexcept { return Next(); }

		/** @brief _length 바이트를 암호학적 난수로 채웁니다(best-effort — OS CSPRNG는 사실상 실패하지 않음). */
		void_t Fill(void_t* _buffer, std::size_t _length) noexcept;

		[[nodiscard]] ulonglong_t Next() noexcept;

	public:
		[[nodiscard]] static constexpr ulonglong_t min() noexcept { return 0; }
		[[nodiscard]] static constexpr ulonglong_t max() noexcept { return (std::numeric_limits<ulonglong_t>::max)(); }
	};

END_NS
