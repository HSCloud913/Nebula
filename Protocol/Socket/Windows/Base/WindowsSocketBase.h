//
// Created by nebula on 24. 5. 29.
//

#ifndef WINDOWSSOCKETBASE_H
#define WINDOWSSOCKETBASE_H

#include <winsock2.h>
#include "Type.h"

BEGIN_NS(ne::protocol)
	class WindowsSocketBase final
	{
		NEBULA_NON_COPYABLE(WindowsSocketBase)

	public:
		WindowsSocketBase()
		{
			auto wsaData = ::WSADATA{};
			if (const auto result = ::WSAStartup(MAKEWORD(2, 2), &wsaData))
			{
				throw ne::Exception("[WindowsSocketBase]", std::format("Failed to initialize Winsock API 2.2 (result: {})", result));
			}
		}
		~WindowsSocketBase()
		{
			if (!isMoved) ::WSACleanup();
		}

		WindowsSocketBase(WindowsSocketBase&& _other) noexcept
		{
			_other.isMoved = true;
		}
		WindowsSocketBase& operator=(WindowsSocketBase&& _other) noexcept
		{
			_other.isMoved = true;
			isMoved = false;

			return *this;
		}

	private:
		bool_t isMoved = false;
	};

END_NS

#endif //WINDOWSSOCKETBASE_H
