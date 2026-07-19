//
// Created by hscloud on 26. 7. 10.
//

#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>
#include "Base/Type.h"
#include "Io/Engine/IEngine.h"

namespace ne::time
{
	class TimerWheel;
}

BEGIN_NS(ne::io)
	class Context;

	/**
	 * @class ContextPool
	 * @brief thread-per-core 실행기 풀. 워커마다 독립된 엔진/Context/TimerWheel/스레드를 소유한다.
	 *
	 * 각 Context 는 단일 스레드 전제로 동작하므로 워커 간 코어 공유나 자원 공유가 없다.
	 * 코루틴/소켓은 자신이 속한 Context 스레드 위에서만 구동해야 하며, 코어 간 이동이 필요하면
	 * 목적지 Context::Post() 로만 명시적으로 넘긴다. Start() 이전에는 Acquire()/At() 로 얻은
	 * Context 를 직접 RunOnce() 로 구동할 수도 있다(스레드를 스폰하지 않아 결정론적).
	 */
	class ContextPool
	{
	public:
		explicit ContextPool(EngineType _engineType, std::size_t _size = 0);
		~ContextPool();

		NEBULA_NON_COPYABLE_MOVABLE(ContextPool)

	private:
		struct Worker
		{
			std::unique_ptr<IEngine> engine;
			std::unique_ptr<ne::time::TimerWheel> timerWheel;
			std::unique_ptr<Context> context;
			std::thread thread;
		};

	private:
		std::vector<Worker> workers;
		std::atomic<std::size_t> cursor{ 0 };
		bool_t isRunning{ false };

	public:
		void_t Start();
		void_t Stop();

	public:
		[[nodiscard]] Context& Acquire() noexcept;
		[[nodiscard]] Context& At(const std::size_t _index) const noexcept { return *workers[_index].context; }
		[[nodiscard]] std::size_t Size() const noexcept { return workers.size(); }
		[[nodiscard]] bool_t IsRunning() const noexcept { return isRunning; }
	};

END_NS
