//
// Created by hscloud on 26. 7. 9.
//

#include "Util/SecureRandom.h"

#include <cstdlib>

#if defined(_WIN32)
#include "Base/WindowsApi.h"
#   include <bcrypt.h>
#elif defined(IS_POSIX)
#   include <cerrno>
#   include <fcntl.h>
#   include <sys/random.h>
#   include <unistd.h>
#endif



namespace ne::util
{
	bool_t SecureRandom::Fill(void_t* _buffer, const std::size_t _length) noexcept
	{
		if (_buffer == nullptr) return false;
		if (_length == 0) return true;

#if defined(_WIN32)
		// BCRYPT_USE_SYSTEM_PREFERRED_RNG: 알고리즘 핸들 없이 시스템 CSPRNG 사용.
		const NTSTATUS status = ::BCryptGenRandom(nullptr, static_cast<PUCHAR>(_buffer), static_cast<ULONG>(_length), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
		return status >= 0; // STATUS_SUCCESS(0) 이상이면 성공
#elif defined(IS_POSIX)
		auto* out = static_cast<ne::byte_t*>(_buffer);
		std::size_t filled = 0;
		while (filled < _length)
		{
			const ssize_t n = ::getrandom(out + filled, _length - filled, 0);
			if (n > 0)
			{
				filled += static_cast<std::size_t>(n);
				continue;
			}
			if (n < 0 && errno == EINTR) continue;
			break; // getrandom 미지원/실패 — /dev/urandom 폴백
		}

		if (filled < _length)
		{
			const int_t fd = ::open("/dev/urandom", O_RDONLY);
			if (fd >= 0)
			{
				while (filled < _length)
				{
					const ssize_t n = ::read(fd, out + filled, _length - filled);
					if (n > 0)
					{
						filled += static_cast<std::size_t>(n);
						continue;
					}
					if (n < 0 && errno == EINTR) continue;
					break;
				}

				::close(fd);
			}
		}

		return filled == _length;
#else
		return false; // 알 수 없는 플랫폼: 엔트로피를 제공할 수 없음 → 실패 보고
#endif
	}

	ulonglong_t SecureRandom::Next() noexcept
	{
		ulonglong_t value = 0;
		if (!Fill(&value, sizeof(value))) std::abort(); // CSPRNG 실패는 복구 불가·치명적 → fail-closed

		return value;
	}
}
