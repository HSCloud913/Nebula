//
// Created by hscloud on 26. 7. 8.
//
// Level 1 — Executor + 이벤트 루프. 단일 스레드 전제(thread-per-core 는 이후 확장).
// 엔진을 구동해 완료를 회수하고, 각 완료의 userData(IoCompletionHandler*)를 통해 대기 중인
// 코루틴을 resume 한다. 다른 스레드가 넘긴 작업은 Post() → Wake() 로 루프에 전달한다.
//
// 핵심 원칙(스펙 3): 코루틴이 어느 컨텍스트에 속하는지 항상 명확해야 하며, 코어 간 이동은
// Post() 로만 명시적으로 허용한다(암묵적 마이그레이션 금지).

#pragma once
#include <atomic>
#include <chrono>
#include <coroutine>
#include <mutex>
#include <vector>
#include "Type.h"
#include "Engine/IIoEngine.h"

namespace ne::time
{
	class TimerWheel;
}

BEGIN_NS(ne::io)
	// 완료 디스패치 규약. 엔진에 제출하는 IoRequest.userData 는 이 구조체를 가리킨다 —
	// 루프가 완료 시 result 를 채우고(completed=true) handle 을 resume 한다.
	//
	// mid-flight 수명 안전(스펙 4·Task.h 경고): 진행 중 I/O 상태로 코루틴 프레임이 파괴되면
	// Level 2 awaitable 이 이 핸들러를 heap 에 두고 abandoned=true 로 표시해 소유권을 루프에 넘긴다.
	// 루프는 완료 회수 시 abandoned 면 resume 없이 delete 하여 use-after-free 를 막는다.
	struct IoCompletionHandler
	{
		std::coroutine_handle<> handle{};
		longlong_t result{ 0 };
		bool_t isCompleted{ false };
		bool_t isAbandoned{ false };
	};

	class IoContext
	{
	public:
		explicit IoContext(IIoEngine& _engine) noexcept;
		~IoContext() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IoContext)

	private:
		static constexpr int_t MaxBatch = 128; // WaitCompletions 한 번에 회수할 완료 상한

	private:
		IIoEngine& engine;
		ne::time::TimerWheel* timerWheel;                       // optional — 있으면 루프가 Tick/타임아웃 반영
		std::mutex postMutex;                        // postedHandles 보호(다른 스레드 Post)
		std::vector<std::coroutine_handle<>> postedHandles;     // Post 로 넘어와 다음 루프에서 resume 될 작업
		std::atomic<bool_t> isRunning{ false };
		// Run() 이 실제로 시작하기 전에(다른 스레드가 스폰 직후 곧장) Stop() 이 먼저 도착하면,
		// Run() 진입부의 running=true 세팅이 그 Stop() 을 덮어써 무기한 대기에 빠질 수 있다 —
		// 그 경합을 잡기 위한 플래그(자세한 내용은 IoContext.cpp 참고).
		std::atomic<bool_t> isStopRequested{ false };

	public:
		// 완료/타이머/Post 작업이 소진되도록 Stop() 까지 계속 루프한다.
		void_t Run();
		// 한 번의 루프: 완료를 _timeout 동안 대기·디스패치하고 타이머 Tick + Post 작업을 처리한다.
		// 완료를 하나라도 디스패치했으면 true.
		bool_t RunOnce(std::chrono::milliseconds _timeout);
		// 다른 스레드(또는 콜백)에서 코루틴을 이 컨텍스트 루프에 재개 예약한다 — Wake() 로 루프를 깨운다.
		void_t Post(std::coroutine_handle<> _handle);
		// Run() 루프를 종료시킨다(다음 iteration 에서 빠져나옴).
		void_t Stop() noexcept;
		void_t SetTimerWheel(ne::time::TimerWheel* _timerWheel) noexcept { timerWheel = _timerWheel; }

	private:
		// timerWheel 이 있으면 다음 만료까지, 없으면 _timeout 을 그대로 유효 타임아웃으로 계산.
		[[nodiscard]] std::chrono::milliseconds EffectiveTimeout(std::chrono::milliseconds _timeout) const noexcept;
		void_t DrainPosted();

	public:
		[[nodiscard]] IIoEngine& Engine() const noexcept { return engine; }
		[[nodiscard]] bool_t IsRunning() const noexcept { return isRunning.load(std::memory_order_acquire); }
	};

END_NS
