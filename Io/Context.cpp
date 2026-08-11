//
// Created by hscloud on 26. 7. 8.
//

#include "Io/Context.h"

#include <cassert>
#include "Time/TimerQueue.h"



namespace ne::io
{
	Context::Context(IEngine& _engine, ne::time::TimerQueue* _timer)
		: engine(_engine)
		, ownedTimer(_timer == nullptr ? std::make_unique<ne::time::TimerQueue>() : nullptr)
		, timer(_timer != nullptr ? _timer : ownedTimer.get()) {}

	// TimerQueue 이 불완전 타입이 아니어야 unique_ptr 소멸이 가능하므로 여기(.cpp)에서 정의한다.
	Context::~Context() = default;



	void_t Context::Start()
	{
		// IDLE → RUNNING 으로 원자적으로 진입한다. CAS 가 실패하는 두 경우:
		//  - RUNNING: 이미 다른 스레드가 구동 중이므로 중복 호출로 보고 그냥 반환.
		//  - STOP_REQUESTED: 우리가 진입하기 전에 Stop() 이 도착했다. 요청을 소비해 IDLE 로 되돌리고
		//    반환한다(플래그를 남겨두면 다음 Start() 가 이유 없이 즉시 빠져나온다).
		RunState expected = RunState::IDLE;
		if (!state.compare_exchange_strong(expected, RunState::RUNNING, std::memory_order_acq_rel))
		{
			if (expected == RunState::STOP_REQUESTED) (void_t)state.compare_exchange_strong(expected, RunState::IDLE, std::memory_order_acq_rel);
			return;
		}

		while (state.load(std::memory_order_acquire) == RunState::RUNNING) (void_t)RunOnce(std::chrono::milliseconds{ -1 });

		// 루프를 빠져나왔다 = STOP_REQUESTED 를 관측했다. 다음 Start() 가 정상 동작하도록 IDLE 로 되돌린다.
		RunState observed = RunState::STOP_REQUESTED;
		(void_t)state.compare_exchange_strong(observed, RunState::IDLE, std::memory_order_acq_rel);
	}

	void_t Context::Stop() noexcept
	{
		// 현재 상태가 무엇이든 정지 요청으로 덮어쓴다. Start() 의 CAS 와 짝을 이뤄, "Stop 이 Start 의
		// 루프 진입 직전에 도착" 하는 창에서도 요청이 사라지지 않는다(과거 무한 대기/join 멈춤의 원인).
		(void_t)state.exchange(RunState::STOP_REQUESTED, std::memory_order_acq_rel);

		engine.Wake();
	}


	bool_t Context::RunOnce(const std::chrono::milliseconds _timeout)
	{
		Completion completions[MaxBatch];
		const int_t count = engine.WaitCompletions(completions, MaxBatch, EffectiveTimeout(_timeout));

		// 처리 전에 배치 전체를 "회수됨" 으로 표시한다. 아래 resume 중에 배치 뒤쪽 op 의 대기자가 파괴될
		// 수 있는데, 그 op 은 이미 엔진에서 빠졌으므로 취소를 요청하면 안 된다(CompletionHandler 주석 참고).
		for (int_t i = 0; i < count; ++i)
		{
			if (auto* handler = static_cast<CompletionHandler*>(completions[i].userData); handler != nullptr) handler->isHarvested = true;
		}

		for (int_t i = 0; i < count; ++i)
		{
			auto* handler = static_cast<CompletionHandler*>(completions[i].userData);
			if (handler == nullptr) continue;

			handler->result = completions[i].result;

			// resume 이 대기자를 파괴하면 그 즉시 handler 도 해제될 수 있으므로, 상태를 바꾸기 전에
			// 핸들을 지역 변수로 복사해 둔다.
			const std::coroutine_handle<> handle = handler->handle;

			// 소유권 인계: PENDING → COMPLETED. 직전 상태가 ABANDONED 면 대기자가 이미 사라진 것이므로
			// 여기서 해제하고, 그 경우 resume 할 코루틴도 없다.
			if (handler->state.exchange(HandlerState::COMPLETED, std::memory_order_acq_rel) == HandlerState::ABANDONED)
			{
				delete handler;
				continue;
			}

			if (handle && !handle.done()) handle.resume();
		}

		if (timer != nullptr) timer->Tick();
		DrainPosted();

		return count > 0;
	}

	void_t Context::Post(const std::coroutine_handle<> _handle)
	{
		{
			std::lock_guard lock(postMutex);
			postedHandles.push_back(_handle);
		}

		engine.Wake();
	}



	ne::time::Awaitable Context::SleepFor(const std::chrono::milliseconds _duration) const noexcept
	{
		// 생성자가 항상 휠을 확보하므로 nullptr 일 수 없다.
		return ne::time::SleepFor(*timer, _duration);
	}



	std::chrono::milliseconds Context::EffectiveTimeout(const std::chrono::milliseconds _timeout) const noexcept
	{
		if (timer == nullptr) return _timeout;

		const int_t nextExpiry = timer->NextExpiryMs();
		if (nextExpiry < 0) return _timeout;

		if (_timeout.count() < 0 || nextExpiry < _timeout.count()) return std::chrono::milliseconds{ nextExpiry };

		return _timeout;
	}

	void_t Context::DrainPosted()
	{
		std::vector<std::coroutine_handle<>> pending;
		{
			std::lock_guard lock(postMutex);
			pending.swap(postedHandles);
		}

		for (const std::coroutine_handle<> handle : pending)
		{
			if (handle && !handle.done()) handle.resume();
		}
	}
}
