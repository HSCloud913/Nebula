//
// Created by hscloud on 26. 7. 10.
//

#include "Io/Context/ContextPool.h"

#include <algorithm>
#include "Io/Context/Context.h"
#include "Time/Timer/TimerWheel.h"

#if defined(_WIN32)
#	include "Io/Engine/Iocp/IocpEngine.h"
#	include "Io/Engine/WsaPoll/WsaPollEngine.h"
#elif defined(IS_POSIX)
#	include "Io/Engine/IoUring/IoUringEngine.h"
#	include "Io/Engine/Epoll/EpollEngine.h"
#endif



BEGIN_NS(ne::io)
	ContextPool::ContextPool(const EngineType _engineType, const std::size_t _size)
	{
		const std::size_t size = _size > 0 ? _size : std::max<std::size_t>(1, std::thread::hardware_concurrency());
		workers.reserve(size);

		for (std::size_t i = 0; i < size; ++i)
		{
			Worker worker;
#if defined(_WIN32)
			if (_engineType == EngineType::REACTOR)
			{
				worker.engine = std::make_unique<WsaPollEngine>();
			}
			else if (_engineType == EngineType::PROACTOR)
			{
				worker.engine = std::make_unique<IocpEngine>();
			}
#elif defined(IS_POSIX)
			if (_engineType == EngineType::REACTOR)
			{
				worker.engine = std::make_unique<EpollEngine>();
			}
			else if (_engineType == EngineType::PROACTOR)
			{
				worker.engine = std::make_unique<IoUringEngine>();
			}
#endif
			worker.timerWheel = std::make_unique<ne::time::TimerWheel>();
			worker.context = std::make_unique<Context>(*worker.engine, worker.timerWheel.get());

			workers.push_back(std::move(worker));
		}
	}

	ContextPool::~ContextPool() { Stop(); }



	void_t ContextPool::Start()
	{
		if (isRunning) return;
		isRunning = true;

		for (auto& worker : workers)
		{
			worker.thread = std::thread([context = worker.context.get()] { context->Start(); });
		}
	}

	void_t ContextPool::Stop()
	{
		if (!isRunning) return;
		isRunning = false;

		for (const auto& worker : workers) worker.context->Stop();
		for (auto& worker : workers)
		{
			if (worker.thread.joinable()) worker.thread.join();
		}
	}



	Context& ContextPool::Acquire() noexcept
	{
		const std::size_t index = cursor.fetch_add(1, std::memory_order_relaxed) % workers.size();
		return *workers[index].context;
	}

END_NS
