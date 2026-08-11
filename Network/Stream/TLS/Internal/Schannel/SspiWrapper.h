//
// Created by hscloud on 25. 6. 30.
//

#pragma once

#ifdef _WIN32
#	ifndef SECURITY_WIN32
#		define SECURITY_WIN32
#	endif
#	include <security.h>
#	include "Base/Type.h"

namespace ne::network::internal
{
	/**
	 * @class SspiWrapper
	 * @brief secur32.dll 을 최초 1회만 로드해(Meyers singleton) SSPI 함수 테이블을 제공하는 예외 없는 로더입니다.
	 */
	struct SspiWrapper
	{
		/** @brief SSPI 함수 테이블을 반환합니다. 최초 호출 시에만 secur32.dll 을 로드하며, 실패하면 nullptr 을 반환합니다. */
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
}

#endif // _WIN32
