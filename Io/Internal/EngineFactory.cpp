//
// Created by hscloud on 26. 7. 24.
//

#include "Io/Engine.h"

#if defined(_WIN32)
#	include "Io/Internal/Engine/Iocp/IocpEngine.h"
#	include "Io/Internal/Engine/WsaPoll/WsaPollEngine.h"
#elif defined(IS_POSIX)
#	include "Io/Internal/Engine/IoUring/IoUringEngine.h"
#	include "Io/Internal/Engine/Epoll/EpollEngine.h"
#endif



namespace ne::io
{
	std::unique_ptr<IEngine> MakeEngine(const EngineType _type)
	{
#if defined(_WIN32)
		if (_type == EngineType::REACTOR) return std::make_unique<WsaPollEngine>();
		if (_type == EngineType::PROACTOR) return std::make_unique<IocpEngine>();
#elif defined(IS_POSIX)
		if (_type == EngineType::REACTOR) return std::make_unique<EpollEngine>();
		if (_type == EngineType::PROACTOR) return std::make_unique<IoUringEngine>();
#endif
		return nullptr;
	}
}
