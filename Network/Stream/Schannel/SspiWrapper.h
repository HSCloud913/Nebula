//
// Created by hscloud on 25. 6. 30.
//
// No-throw, no-singleton SSPI loader.
// Loads secur32.dll once (Meyers singleton) and exposes the function table.
//

#pragma once
#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <Windows.h>
#include <security.h>
#include <schannel.h>

#include "Type.h"

BEGIN_NS(ne::network)

struct SspiWrapper
{
	[[nodiscard]] static PSecurityFunctionTableW Get() noexcept
	{
		static PSecurityFunctionTableW fn = []() noexcept -> PSecurityFunctionTableW
		{
			HMODULE h = ::LoadLibraryA("secur32.dll");
			if (!h) return nullptr;

			using InitFn = PSecurityFunctionTableW(WINAPI*)();
			auto* init = reinterpret_cast<InitFn>(
				::GetProcAddress(h, "InitSecurityInterfaceW"));
			return init ? init() : nullptr;
		}();
		return fn;
	}
};

END_NS
#endif // _WIN32
