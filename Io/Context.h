//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include <atomic>
#include <chrono>
#include <coroutine>
#include <memory>
#include <mutex>
#include <vector>
#include "Base/Type.h"
#include "Base/Coroutine/IExecutor.h"
#include "Io/Engine.h"
#include "Time/Sleep.h"

namespace ne::io
{
	/**
	 * @class HandlerState
	 * @brief CompletionHandler 의 소유권 상태입니다.
	 *
	 * 대기자(Awaitable)와 이벤트 루프 중 **나중에 상태를 바꾼 쪽이 해제 책임을 진다**는 규약을 단일
	 * 원자 변수로 표현합니다. 과거에는 isCompleted/isAbandoned 두 개의 비원자 bool 이었고, 양쪽이
	 * 서로의 갱신을 보지 못하면 이중 해제 또는 영구 누수가 났습니다.
	 */
	enum class HandlerState : byte_t
	{
		PENDING,   // 커널이 op 을 들고 있음 — 어느 쪽도 해제하지 않는다
		COMPLETED, // 루프가 완료를 회수함 — 대기자가 결과를 읽고 해제한다
		ABANDONED, // 대기자가 먼저 사라짐 — 루프가 완료를 회수할 때 해제한다
	};

	/**
	 * @class CompletionHandler
	 * @brief 엔진에 제출한 I/O 요청 하나의 완료를 코루틴 재개로 연결하는 디스패치 단위.
	 *
	 * 엔진에 넘기는 Request.userData 는 이 구조체를 가리킨다. Context 의 이벤트 루프가 완료를
	 * 회수하면 result 를 채우고 state 를 COMPLETED 로 교체한 뒤 handle 을 resume 한다.
	 *
	 * @note addressStorage 는 커널이 **완료 시점까지** 읽거나 쓰는 sockaddr 을 담는다. 이것이 코루틴
	 * 프레임에 있으면(과거 구조) 취소/타임아웃으로 프레임이 먼저 파괴됐을 때 커널이 해제된 메모리에
	 * 접근한다. 수명이 op 과 일치하는 이 구조체가 소유해야 안전하다. sockaddr 타입을 직접 쓰지 않는
	 * 이유는 이 헤더가 저장소 전반에 include 되어 <winsock2.h> 를 다시 퍼뜨리게 되기 때문이다 —
	 * 크기/정렬 일치는 Socket.cpp 에서 static_assert 로 못박는다.
	 */
	struct CompletionHandler
	{
		static constexpr std::size_t AddressStorageSize = 128; // sizeof(sockaddr_storage), Windows·Linux 공통

		std::coroutine_handle<> handle{};
		longlong_t result{ 0 };
		std::atomic<HandlerState> state{ HandlerState::PENDING };

		// 루프가 이 op 의 완료를 엔진에서 이미 **회수**했는지. 커널이 더 이상 op 을 들고 있지 않다는 뜻이다.
		//
		// 필요한 이유: 한 배치 안에서 앞쪽 완료를 resume 하다가 뒤쪽 완료의 대기자가 파괴되는 일이 있다
		// (연결 teardown 등). 그때 상태는 아직 PENDING 이므로 소멸자가 Cancel 을 요청하는데, 그 op 은 이미
		// 엔진의 추적 맵에서 빠져 있다. 곧 이 핸들러가 해제되고 같은 주소에 새 핸들러가 할당되면, 뒤늦게
		// 처리되는 그 취소 요청이 **무관한 새 op** 을 취소해 버린다. 회수 사실을 미리 표시해 취소 자체를
		// 생략한다. 루프 스레드에서만 읽고 쓰므로(대기자 파괴도 그 스레드에서 일어난다) 원자성은 불필요하다.
		bool_t isHarvested{ false };

		alignas(8) byte_t addressStorage[AddressStorageSize]{};
		int_t addressLength{ 0 };
	};

	/**
	 * @class Context
	 * @brief 단일 스레드 위에서 구동되는 executor 겸 I/O 이벤트 루프.
	 *
	 * 엔진을 구동해 완료를 회수하고, 각 완료의 userData(CompletionHandler*)를 통해 대기 중인
	 * 코루틴을 resume 한다. 매 루프에서 타이머 큐를 Tick 하며, 다른 스레드가 Post() 로
	 * 넘긴 작업은 Wake() 로 루프를 깨워 다음 iteration 에서 처리한다. 코루틴은 자신이 속한
	 * Context 스레드 위에서만 구동되어야 하며, 코어 간 이동은 Post() 로만 명시적으로 이뤄진다.
	 *
	 * ne::IExecutor 를 구현하므로, Base 계층의 코루틴 프리미티브(Event::SignalDeferred, WhenAny)에
	 * 지연 재개 실행자로 그대로 넘길 수 있다.
	 */
	class Context final : public IExecutor
	{
	public:
		/**
		 * @param _timer 공유할 타이머 큐(예: ContextPool 이 워커마다 소유한 것). nullptr 이면
		 * **이 Context 가 자체 타이머 큐를 소유한다.**
		 *
		 * @note 예전에는 nullptr 이면 타이머가 아예 없어 SleepFor()/Timeout() 이 assert 로 죽었다.
		 * 타이머는 데드라인의 전제이고 데드라인은 서버 보안 장치의 전제이므로, "타이머 없는 Context" 는
		 * 값싼 편의가 아니라 함정이었다. 큐 자체는 빈 힙 + 빈 셋이라 소유 비용이 사실상 없다.
		 */
		explicit Context(IEngine& _engine, ne::time::TimerQueue* _timer = nullptr);
		~Context() override;

		NEBULA_NON_COPYABLE_MOVABLE(Context)

	private:
		static constexpr int_t MaxBatch = 128;

		/**
		 * @class RunState
		 * @brief Start()/Stop() 핸드셰이크 상태입니다.
		 *
		 * "실행 중" 과 "정지 요청됨" 을 별개 bool 두 개로 두면, Start() 가 정지 플래그를 확인한 뒤
		 * 실행 플래그를 세우기 **전** 에 Stop() 이 끼어드는 창이 생겨 요청이 사라집니다(그 결과 루프가
		 * 영원히 돌아 join 이 멈춤). 단일 원자 상태 + CAS 로 그 창을 없앱니다.
		 */
		enum class RunState : byte_t
		{
			IDLE,           // 루프가 돌지 않는 상태
			RUNNING,        // 루프 진행 중
			STOP_REQUESTED, // 정지 요청 — 돌고 있으면 곧 빠져나오고, 아직 진입 전이면 진입 자체를 막는다
		};

	private:
		IEngine& engine;
		std::unique_ptr<ne::time::TimerQueue> ownedTimer; // 주입받지 않았을 때만 채워진다
		ne::time::TimerQueue* timer;                      // 주입본 또는 ownedTimer
		std::mutex postMutex;
		std::vector<std::coroutine_handle<>> postedHandles;
		std::atomic<RunState> state{ RunState::IDLE };

	public:
		void_t Start();
		void_t Stop() noexcept;

		bool_t RunOnce(std::chrono::milliseconds _timeout);
		void_t Post(std::coroutine_handle<> _handle) override;

		[[nodiscard]] ne::time::Awaitable SleepFor(std::chrono::milliseconds _duration) const noexcept;

	private:
		[[nodiscard]] std::chrono::milliseconds EffectiveTimeout(std::chrono::milliseconds _timeout) const noexcept;
		void_t DrainPosted();

	public:
		[[nodiscard]] IEngine& Engine() const noexcept { return engine; }
		[[nodiscard]] bool_t IsRunning() const noexcept { return state.load(std::memory_order_acquire) == RunState::RUNNING; }
	};
}
