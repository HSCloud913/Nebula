//
// Created by hscloud on 25. 6. 29.
//

#include "Error.h"

#if defined(_WIN32)
#   include <windows.h>
#elif defined(IS_POSIX)
#   include <cstring>
#endif

BEGIN_NS(ne)
	Error::Error(const string_view_t _message)
		: message(_message) {}



	Error& Error::Context(string_view_t _context)
	{
		if (contextChain.empty()) contextChain = std::format("{} -> ", _context);
		else contextChain = std::format("{} -> {}", _context, contextChain);

		return *this;
	}


	string_view_t Error::Message() const noexcept
	{
		return message;
	}

	string_t Error::What() const
	{
		if (contextChain.empty()) return message;
		return contextChain + message;
	}



	/*----------------------------------------------------------------------------------------------------*/



	OsError::OsError(const ulong_t _code)
		: Error(OsMessage(_code))
		, code(_code) {}

	OsError::OsError(const ulong_t _code, const string_view_t _message)
		: Error(_message)
		, code(_code) {}



	string_t OsError::OsMessage(const ulong_t _code)
	{
#if defined(_WIN32)
		char_t buffer[512]{};
		::FormatMessageA(
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			static_cast<DWORD>(_code),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			buffer,
			static_cast<DWORD>(sizeof(buffer) - 1),
			nullptr
		);

		string_t msg(buffer);
		while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n')) msg.pop_back();

		return std::format("{} [{}]", msg.empty() ? "Unknown error" : msg, _code);
#elif defined(IS_POSIX)
		return std::format("{} [{}]", ::strerror(static_cast<int>(_code)), _code);
#else
		return std::format("OS error [{}]", _code);
#endif
	}

END_NS
