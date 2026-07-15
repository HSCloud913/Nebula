//
// Created by nebula on 24. 6. 14.
//

#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <thread>
#include "Base/Type.h"

BEGIN_NS(ne)
	/**
	 * @class ThreadPool
	 * @brief 고정 개수의 워커 스레드로 작업(job) 큐를 처리하는 스레드 풀입니다.
	 *
	 * Enqueue()로 넘긴 호출 가능 객체는 std::future로 결과를 돌려받을 수 있으며,
	 * Shutdown() 이후 Enqueue()는 무효한 future를 반환합니다.
	 */
	class ThreadPool
	{
	public:
		explicit ThreadPool(size_t _count);
		~ThreadPool() { Shutdown(); }

	private:
		std::vector<std::thread> threads;
		std::queue<std::function<void_t()>> jobs;

		std::condition_variable conditionVariable;
		std::mutex mutex;
		bool_t isShutdown{ false };

	public:
		template <typename F>
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
