//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <format>
#include "Type.h"

#if defined(_WIN32)
#	include <errhandlingapi.h>
#	include <winbase.h>
#elif defined(IS_POSIX)
#	include <cerrno>
#   include <cstring>
#endif

BEGIN_NS(ne)
	class Error
	{
	public:
		Error() = default;
		explicit Error(string_view_t _message)
			: message(_message) {}

	protected:
		string_t message;

	private:
		string_t contextChain;

	public:
		Error& Context(string_view_t _context)
		{
			if (contextChain.empty())
			{
				contextChain = std::format("{} -> ", _context);
			}
			else
			{
				contextChain = std::format("{} -> {}", _context, contextChain);
			}

			return *this;
		}

		[[nodiscard]] string_view_t Message() const noexcept { return message; }
		[[nodiscard]] string_t What() const { return contextChain.empty() ? message : contextChain + message; }
	};

	class OsError :public Error
	{
	public:
		explicit OsError(const ulong_t _code)
			: Error(OsMessage(_code))
			, code(_code) {}

		OsError(const ulong_t _code, string_view_t _message)
			: Error(_message)
			, code(_code) {}

	private:
		ulong_t code{};

	public:
		OsError& Context(string_view_t _context)
		{
			Error::Context(_context);
			return *this;
		}

		[[nodiscard]] ulong_t Code() const noexcept { return code; }

	private:
		[[nodiscard]] static string_t OsMessage(const ulong_t _code)
		{
#if defined(_WIN32)
			char_t buffer[512]{};
			::FormatMessageA(
				FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr,
				_code,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				buffer,
				sizeof(buffer) - 1,
				nullptr
			);

			string_t msg(buffer);
			while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n')) msg.pop_back();

			return std::format("[{}] {}", _code, msg.empty() ? "Unknown error" : msg);
#elif defined(IS_POSIX)
			return std::format("[{}] {}", _code, ::strerror(static_cast<int>(_code)));
#else
			return std::format("OS error [{}]", _code);
#endif
		}
	};

	class HttpError :public Error
	{
	public:
		explicit HttpError(string_view_t _message)
			: Error(_message) {}

		HttpError(const uint16_t _statusCode, string_view_t _message)
			: Error(HttpMessage(_statusCode, _message))
			, statusCode(_statusCode) {}

	private:
		uint16_t statusCode{ 0 };

	public:
		HttpError& Context(string_view_t _context)
		{
			Error::Context(_context);
			return *this;
		}

	public:
		[[nodiscard]] uint16_t StatusCode() const noexcept { return statusCode; }

	private:
		[[nodiscard]] static string_t HttpMessage(const ulong_t _statusCode, string_view_t _message)
		{
			return std::format("[{}] {}", _statusCode, _message.empty() ? "Unknown error" : _message);
		}
	};

#if defined(_WIN32)
	[[nodiscard]] inline ulong_t LastOsError() noexcept { return static_cast<ulong_t>(::GetLastError()); }
#elif defined(IS_POSIX)
	[[nodiscard]] inline ulong_t LastOsError() noexcept { return static_cast<ulong_t>(errno); }
#endif

END_NS
