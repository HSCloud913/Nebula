//
// Created by hscloud on 25. 6. 30.
//
// No-throw, no-singleton SSPI loader.
// Loads secur32.dll once (Meyers singleton) and exposes the function table.
//

#pragma once
#ifdef _WIN32

#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <Windows.h>
#include <security.h>
#include <schannel.h>

#include "Base/Type.h"

BEGIN_NS(ne::network)
	struct SspiWrapper
	{
		[[nodiscard]] static PSecurityFunctionTableW Get() noexcept
		{
			static PSecurityFunctionTableW function = []() noexcept -> PSecurityFunctionTableW
			{
				const HMODULE handle = ::LoadLibraryA("secur32.dll");
				if (!handle) return nullptr;

				using FunctionTable = PSecurityFunctionTableW(WINAPI*)();
				auto* functionTable = reinterpret_cast<FunctionTable>(::GetProcAddress(handle, "InitSecurityInterfaceW"));

				return functionTable ? functionTable() : nullptr;
			}();

			return function;
		}
	};

END_NS
#endif // _WIN32
