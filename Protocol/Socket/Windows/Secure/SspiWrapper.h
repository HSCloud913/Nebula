//
// Created by nebula on 24. 5. 29.
//

#ifndef SSPIWRAPPER_H
#define SSPIWRAPPER_H

#ifdef _WIN32
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	ifndef SECURITY_WIN32
#		define SECURITY_WIN32
#	endif
#endif
#include <Windows.h>
#include <security.h>

#include "Interface/ISingleton.h"

BEGIN_NS(ne::protocol)
	class SspiWrapper final :public ISingleton<SspiWrapper>
	{
		friend class ISingleton<SspiWrapper>;

	private:
		explicit SspiWrapper()
			: handle(LoadLibrary("secur32.dll"))
			, functions(nullptr)
		{
			if (!handle)
			{
				throw ne::Exception("[SspiWrapper]", std::format("Failed to LoadLibrary function (secur32.dll) (error: {})", GetLastError()));
			}

			functions = reinterpret_cast<INIT_SECURITY_INTERFACE_W>(reinterpret_cast<INT_PTR>(GetProcAddress(handle.Get(), "InitSecurityInterfaceW")))();
			if (!functions)
			{
				throw ne::Exception("[SspiWrapper]", std::format("Failed to GetProcAddress function (InitSecurityInterfaceW) (error: {})", GetLastError()));
			}
		}

	private:
		using DllHandle = ne::Handle<HMODULE, decltype([](const auto _h)
		{
			FreeLibrary(_h);
		})>;

		DllHandle handle;

	public:
		PSecurityFunctionTableW functions;
	};

END_NS

#endif //SSPIWRAPPER_H
