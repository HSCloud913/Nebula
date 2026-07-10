//
// Created by hscloud on 26. 7. 10.
//

#include "Io/Engine/EngineFactory.h"

#if defined(_WIN32)
#	include "Io/Engine/Iocp/IocpEngine.h"
#	include "Io/Engine/WsaPoll/WsaPollEngine.h"
#elif defined(IS_POSIX)
#	include "Io/Engine/IoUring/IoUringEngine.h"
#	include "Io/Engine/Epoll/EpollEngine.h"
#endif

BEGIN_NS(ne::io)
	std::unique_ptr<IEngine> MakeDefaultEngine()
	{
#if defined(_WIN32)
		auto primary = std::make_unique<IocpEngine>();     // IOCP(완료형) 우선
		if (primary->IsValid()) return primary;
		return std::make_unique<WsaPollEngine>();          // 폴백: WSAPoll 리액터
#elif defined(IS_POSIX)
		auto primary = std::make_unique<IoUringEngine>();  // io_uring(완료형) 우선
		if (primary->IsValid()) return primary; return std::make_unique<EpollEngine>();            // 폴백: epoll 리액터
#else
		return nullptr;
#endif
	}

END_NS
