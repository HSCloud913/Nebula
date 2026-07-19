//
// Created by nebula on 24. 6. 14.
//

#include "Concurrency/ThreadPool.h"



BEGIN_NS(ne)
	ThreadPool::ThreadPool(const size_t _count)
	{
		threads.reserve(_count);

		for (size_t i = 0; i < _count; i++)
		{
			threads.emplace_back([this]()
			{
				while (true)
				{
					// producer/consumer 패턴의 정석 구현:
					// wait에 predicate를 함께 넘기면 "조건이 거짓인 동안 wait → 깨어나면 predicate 재검사"를 자동 반복하므로
					// spurious wakeup(가짜로 깨어나는 현상)이 발생해도 조건이 실제로 충족될 때까지 안전하게 재대기한다.
					std::unique_lock lock(mutex);

					conditionVariable.wait(lock, [this]() { return !this->jobs.empty() || isShutdown; });

					if (this->jobs.empty() && isShutdown) break;

					auto job = std::move(jobs.front());
					jobs.pop();

					// job 실행 전에 lock을 해제한다: 락을 쥔 채로 job()을 호출하면 락 보유 시간이 길어져
					// 다른 워커가 큐에 접근하지 못해 병목이 생기고, job 내부에서 다시 이 mutex를 잠그는 경우
					// 재진입 데드락이 발생할 수 있다.
					lock.unlock();

					try { job(); }
					catch (...)
					{
						// A job's exception must not escape the thread's top-level function:
						// doing so would call std::terminate() and bring down the whole process.
					}
				}
			});
		}
	}



	void_t ThreadPool::Shutdown()
	{
		{
			std::unique_lock lock(mutex);

			if (isShutdown) return;
			isShutdown = true;
		}

		conditionVariable.notify_all();

		for (auto& thread : threads) { if (thread.joinable()) thread.join(); }
	}

END_NS
