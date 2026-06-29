//
// Created by nebula on 24. 5. 21.
//

#ifndef NEBULA_EXCEPTION_H
#define NEBULA_EXCEPTION_H

#include <format>
#include "Type.h"

BEGIN_NS(ne)
	class Exception :public std::exception
	{
	public:
		NEBULA_NON_COPYABLE(Exception)
		NEBULA_DEFAULT_MOVE(Exception)

	public:
		Exception() = delete;
		Exception(const string_view_t _module, const string_view_t _message)
			: message(CreateMessage(_module, _message)) {}

	private:
		string_t message;

	private:
		[[nodiscard]] static string_t CreateMessage(const string_view_t _module, const string_view_t _message) noexcept
		{
			try
			{
				return std::format("[{}] {}", _module, _message);
			} catch (...)
			{
				return "Error formatting message";
			}
		}

	public:
		[[nodiscard]] virtual const char_t* what() const noexcept override
		{
			return message.c_str();
		}
	};

END_NS

#endif //NEBULA_EXCEPTION_H
