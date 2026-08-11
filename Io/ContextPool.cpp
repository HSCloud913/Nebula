//
// Created by hscloud on 26. 7. 10.
//

#include "Io/ContextPool.h"

#include <algorithm>
#include "Io/Context.h"
#include "Time/TimerQueue.h"

#if defined(_WIN32)
#	include "Io/Internal/Engine/Iocp/IocpEngine.h"
#	include "Io/Internal/Engine/WsaPoll/WsaPollEngine.h"
#elif defined(IS_POSIX)
#	include "Io/Internal/Engine/IoUring/IoUringEngine.h"
#	include "Io/Internal/Engine/Epoll/EpollEngine.h"
#endif



namespace ne::io
{
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
			worker.timer = std::make_unique<ne::time::TimerQueue>();
			worker.context = std::make_unique<Context>(*worker.engine, worker.timer.get());

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
}
