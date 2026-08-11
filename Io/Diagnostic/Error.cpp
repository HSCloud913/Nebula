//
// Created by hscloud on 26. 8. 11.
//

#include "Io/Diagnostic/Error.h"

#if defined(_WIN32)
#	include "Base/WinsockApi.h"
#elif defined(IS_POSIX)
#	include <cerrno>
#endif



namespace ne::io
{
	// 여기 목록은 "이식 가능한 사용자 코드가 실제로 분기해야 하는 것"만 담는다. 나머지는 OS_FAILURE 로
	// 남겨 Code() 로 진단하게 한다 — 어휘를 넓히면 그만큼 두 플랫폼을 계속 맞춰줘야 한다.
	IoErrorKind ClassifyOsError(const ne::ulong_t _code) noexcept
	{
#if defined(_WIN32)
		switch (_code)
		{
			// WSA_OPERATION_ABORTED 는 ERROR_OPERATION_ABORTED 와 같은 값(995)이라 따로 적지 않는다.
			case ERROR_OPERATION_ABORTED: // CancelIoEx 로 취소된 중첩 I/O
			case WSAECANCELLED:
				return IoErrorKind::CANCELLED;

			case WSAECONNRESET:
			case WSAECONNABORTED:
			case WSAESHUTDOWN:
			case WSAENOTCONN:
			case ERROR_BROKEN_PIPE:
			case ERROR_NETNAME_DELETED: // 소켓에서 ReadFile/WriteFile 이 보는 리셋
				return IoErrorKind::CONNECTION_CLOSED;

			default:
				return IoErrorKind::OS_FAILURE;
		}
#elif defined(IS_POSIX)
		switch (static_cast<int>(_code))
		{
			case ECANCELED:
			case EINTR:
				return IoErrorKind::CANCELLED;

			case ECONNRESET:
			case ECONNABORTED:
			case EPIPE:
			case ENOTCONN:
			case ESHUTDOWN:
				return IoErrorKind::CONNECTION_CLOSED;

			default:
				return IoErrorKind::OS_FAILURE;
		}
#else
		(void_t)_code;
		return IoErrorKind::OS_FAILURE;
#endif
	}
}
