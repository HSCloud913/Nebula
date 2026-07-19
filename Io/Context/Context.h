//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include <atomic>
#include <chrono>
#include <coroutine>
#include <mutex>
#include <vector>
#include "Base/Type.h"
#include "Io/Engine/IEngine.h"
#include "Time/Coroutine/Awaitable.h"

BEGIN_NS(ne::io)
	/**
	 * @class CompletionHandler
	 * @brief 엔진에 제출한 I/O 요청 하나의 완료를 코루틴 재개로 연결하는 디스패치 단위.
	 *
	 * 엔진에 넘기는 Request.userData 는 이 구조체를 가리킨다. Context 의 이벤트 루프가 완료를
	 * 회수하면 result 를 채우고 isCompleted 를 세운 뒤 handle 을 resume 한다. 진행 중인 I/O
	 * 상태에서 코루틴 프레임이 먼저 파괴되면 isAbandoned 로 표시되어, 소유권이 루프로 넘어가고
	 * 루프는 resume 대신 delete 로 정리한다.
	 */
	struct CompletionHandler
	{
		std::coroutine_handle<> handle{};
		longlong_t result{ 0 };
		bool_t isCompleted{ false };
		bool_t isAbandoned{ false };
	};

	/**
	 * @class Context
	 * @brief 단일 스레드 위에서 구동되는 executor 겸 I/O 이벤트 루프.
	 *
	 * 엔진을 구동해 완료를 회수하고, 각 완료의 userData(CompletionHandler*)를 통해 대기 중인
	 * 코루틴을 resume 한다. 타이머 휠이 있으면 매 루프에서 Tick 하며, 다른 스레드가 Post() 로
	 * 넘긴 작업은 Wake() 로 루프를 깨워 다음 iteration 에서 처리한다. 코루틴은 자신이 속한
	 * Context 스레드 위에서만 구동되어야 하며, 코어 간 이동은 Post() 로만 명시적으로 이뤄진다.
	 */
	class Context
	{
	public:
		explicit Context(IEngine& _engine, ne::time::TimerWheel* _timerWheel = nullptr) noexcept;
		~Context() = default;

		NEBULA_NON_COPYABLE_MOVABLE(Context)

	private:
		static constexpr int_t MaxBatch = 128;

	private:
		IEngine& engine;
		ne::time::TimerWheel* timerWheel;
		std::mutex postMutex;
		std::vector<std::coroutine_handle<>> postedHandles;
		std::atomic<bool_t> isRunning{ false };
		std::atomic<bool_t> isStopRequested{ false };

	public:
		void_t Start();
		void_t Stop() noexcept;

		bool_t RunOnce(std::chrono::milliseconds _timeout);
		void_t Post(std::coroutine_handle<> _handle);

		[[nodiscard]] ne::time::Awaitable SleepFor(std::chrono::milliseconds _duration) const noexcept;

	private:
		[[nodiscard]] std::chrono::milliseconds EffectiveTimeout(std::chrono::milliseconds _timeout) const noexcept;
		void_t DrainPosted();

	public:
		[[nodiscard]] IEngine& Engine() const noexcept { return engine; }
		[[nodiscard]] bool_t IsRunning() const noexcept { return isRunning.load(std::memory_order_acquire); }
	};

END_NS
