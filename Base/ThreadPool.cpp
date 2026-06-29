//
// Created by nebula on 24. 6. 14.
//

#include "ThreadPool.h"



BEGIN_NS(ne)
	ThreadPool::ThreadPool(const size_t _count)
		: isShutdown(false)
	{
		threads.reserve(_count);
		for (size_t i = 0; i < _count; i++)
		{
			threads.emplace_back([this]()
			{
				while (true)
				{
					std::unique_lock lock(mutex);

					conditionVariable.wait(lock, [this]()
					{
						return !this->jobs.empty() || isShutdown;
					});

					if (this->jobs.empty() && isShutdown) break;

					auto job = std::move(jobs.front());
					jobs.pop();

					lock.unlock();

					try
					{
						job();
					}
					catch (...)
					{
						// A job's exception must not escape the thread's top-level function:
						// doing so would call std::terminate() and bring down the whole process.
					}
				}
			});
		}
	}

	ThreadPool::~ThreadPool()
	{
		Shutdown();
	}



	void_t ThreadPool::Shutdown()
	{
		{
			std::unique_lock lock(mutex);

			if (isShutdown) return;
			isShutdown = true;
		}

		conditionVariable.notify_all();

		for (auto& thread : threads)
	 	{
			if (thread.joinable())
				thread.join();
		}
	}

END_NS
