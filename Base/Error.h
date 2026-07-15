//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <format>
#include "Base/Type.h"

#if defined(IS_POSIX)
#	include <cerrno>
#   include <cstring>
#endif

BEGIN_NS(ne)
	/**
	 * @class Error
	 * @brief 예외 없이 실패를 표현하기 위한 기본 에러 타입입니다.
	 *
	 * 메시지와, Context()로 누적되는 호출 경로 체인을 함께 보관합니다. What()은 이
	 * 경로 체인과 메시지를 합쳐 반환하고, Message()는 원본 메시지만 반환합니다.
	 */
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
			if (contextChain.empty()) { contextChain = std::format("{} -> ", _context); }
			else { contextChain = std::format("{} -> {}", _context, contextChain); }

			return *this;
		}

		[[nodiscard]] string_view_t Message() const noexcept { return message; }
		[[nodiscard]] string_t What() const { return contextChain.empty() ? message : contextChain + message; }
	};

	/**
	 * @class OsError
	 * @brief OS 에러 코드(GetLastError/errno)를 사람이 읽을 수 있는 메시지로 변환해 담는 Error입니다.
	 *
	 * @note 메시지를 직접 지정하지 않으면 플랫폼별 API(FormatMessageA/strerror)로 자동 조회합니다.
	 */
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

	private:
		[[nodiscard]] static string_t OsMessage(const ulong_t _code)
		{
#if defined(_WIN32)
			char_t buffer[512]{};
			::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, _code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, sizeof(buffer) - 1, nullptr);

			string_t msg(buffer);
			while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n')) msg.pop_back();

			return std::format("[{}] {}", _code, msg.empty() ? "Unknown error" : msg);
#elif defined(IS_POSIX)
			return std::format("[{}] {}", _code, ::strerror(static_cast<int>(_code)));
#else
			return std::format("OS error [{}]", _code);
#endif
		}

	public:
		OsError& Context(string_view_t _context)
		{
			Error::Context(_context);
			return *this;
		}

		[[nodiscard]] ulong_t Code() const noexcept { return code; }
	};

#if defined(_WIN32)
	[[nodiscard]] inline ulong_t LastOsError() noexcept { return ::GetLastError(); }
#elif defined(IS_POSIX)
	[[nodiscard]] inline ulong_t LastOsError() noexcept { return static_cast<ulong_t>(errno); }
#endif

END_NS
