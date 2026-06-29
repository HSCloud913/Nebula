//
// Created by nebula on 24. 6. 14.
//

#ifndef NEBULA_THREADPOOL_H
#define NEBULA_THREADPOOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <thread>
#include "Type.h"

BEGIN_NS(ne)
	class ThreadPool
	{
	public:
		explicit ThreadPool(size_t _count);
		~ThreadPool();

	private:
		std::vector<std::thread> threads;
		std::queue<std::function<void_t()>> jobs;

		std::condition_variable conditionVariable;
		std::mutex mutex;
		bool_t isShutdown;

	public:
		template<typename F>
		std::future<std::invoke_result_t<F>> Enqueue(F&& _job)
		{
			using R = std::invoke_result_t<F>;
			auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(_job));
			std::future<R> future = task->get_future();

			{
				std::unique_lock<std::mutex> lock(mutex);
				if (isShutdown) return {};
				jobs.push([task]() { (*task)(); });
			}

			conditionVariable.notify_one();

			return future;
		}

		void_t Shutdown();
	};

END_NS

#endif //NEBULA_THREADPOOL_H
