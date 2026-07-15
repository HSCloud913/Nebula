//
// Created by nebula on 24. 5. 21.
//

#pragma once
#include <format>
#include "Base/Type.h"

BEGIN_NS(ne)
	/**
	 * @class Exception
	 * @brief 모듈명과 메시지를 함께 담는 표준 예외 타입입니다.
	 *
	 * what()이 "[모듈명] 메시지" 형식의 문자열을 반환합니다.
	 */
	class Exception :public std::exception
	{
	public:
		Exception() = delete;
		Exception(const string_view_t _module, const string_view_t _message)
			: message(CreateMessage(_module, _message)) {}

		NEBULA_NON_COPYABLE(Exception)
		NEBULA_DEFAULT_MOVE(Exception)

	private:
		string_t message;

	private:
		[[nodiscard]] static string_t CreateMessage(const string_view_t _module, const string_view_t _message) noexcept
		{
			try { return std::format("[{}] {}", _module, _message); }
			catch (...) { return "Error formatting message"; }
		}

	public:
		[[nodiscard]] virtual const char_t* what() const noexcept override { return message.c_str(); }
	};

END_NS
