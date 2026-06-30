//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <format>
#include "Type.h"

#if defined(_WIN32)
#	ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#	endif
#	include <errhandlingapi.h>
#elif defined(IS_POSIX)
#	include <cerrno>
#endif

BEGIN_NS(ne)
	class Error
	{
	public:
		Error() = default;
		explicit Error(string_view_t _message);

	protected:
		string_t message;

	private:
		string_t contextChain;

	public:
		Error& Context(string_view_t _context);

		[[nodiscard]] string_view_t Message() const noexcept;
		[[nodiscard]] string_t      What()    const;
	};

	class OsError : public Error
	{
	public:
		explicit OsError(ulong_t _code);
		OsError(ulong_t _code, string_view_t _message);

	private:
		ulong_t code{};

	public:
		OsError& Context(const string_view_t _context) { Error::Context(_context); return *this; }

		[[nodiscard]] ulong_t Code() const noexcept { return code; }

	private:
		[[nodiscard]] static string_t OsMessage(ulong_t _code);
	};

	// OS 에러 코드를 ulong_t 로 반환 (Windows: GetLastError, POSIX: errno)
#if defined(_WIN32)
	[[nodiscard]] inline ulong_t LastOsError() noexcept { return static_cast<ulong_t>(::GetLastError()); }
#elif defined(IS_POSIX)
	[[nodiscard]] inline ulong_t LastOsError() noexcept { return static_cast<ulong_t>(errno); }
#endif

	class HttpError : public Error
	{
	public:
		explicit HttpError(string_view_t _message);
		HttpError(uint16_t _statusCode, string_view_t _message);

		HttpError& Context(string_view_t _context) { Error::Context(_context); return *this; }

		[[nodiscard]] uint16_t StatusCode() const noexcept { return statusCode; }

	private:
		uint16_t statusCode{};
	};

END_NS
