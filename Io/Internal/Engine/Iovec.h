//
// Created by hscloud on 26. 8. 2.
//

#pragma once

#if defined(IS_POSIX)

#include <sys/uio.h>
#include <vector>
#include "Memory/Buffer/BufferChain.h"

namespace ne::io::internal
{
	/** @brief BufferChain 세그먼트를 readv/writev 용 iovec 배열로 변환합니다(POSIX 전용 — 엔진 내부용). */
	[[nodiscard]] inline std::vector<iovec> ToIovec(const ne::memory::BufferChain& _chain)
	{
		std::vector<iovec> v;
		v.reserve(_chain.Segments().size());
		for (const auto& segment : _chain.Segments()) v.push_back({ segment.ptr, segment.length });

		return v;
	}
}

#endif
