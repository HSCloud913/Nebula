//
// Created by hscloud on 26. 7. 9.
//

#include "Cryptography/Random/SecureRandom.h"

#if defined(_WIN32)
#   include <windows.h>
#   include <bcrypt.h>
#elif defined(IS_POSIX)
#   include <cerrno>
#   include <fcntl.h>
#   include <sys/random.h>
#   include <unistd.h>
#endif



BEGIN_NS(ne::crypto)
	void_t SecureRandom::Fill(void_t* _buffer, const std::size_t _length) noexcept
	{
		if (_buffer == nullptr || _length == 0) return;

#if defined(_WIN32)
		// BCRYPT_USE_SYSTEM_PREFERRED_RNG: 알고리즘 핸들 없이 시스템 CSPRNG 사용.
		(void_t)::BCryptGenRandom(nullptr, static_cast<PUCHAR>(_buffer), static_cast<ULONG>(_length), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#elif defined(IS_POSIX)
		auto* out = static_cast<ne::byte_t*>(_buffer); std::size_t filled = 0; while (filled < _length)
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
#endif
	}

	ulonglong_t SecureRandom::Next() noexcept
	{
		ulonglong_t value = 0;
		Fill(&value, sizeof(value));

		return value;
	}

END_NS
