//
// Created by hscloud on 26. 7. 8.
//

#include "Io/Context/Context.h"

#include <limits>
#include "Time/Timer/TimerWheel.h"

BEGIN_NS(ne::io)
	Context::Context(IEngine& _engine) noexcept
		: engine(_engine)
		, timerWheel(static_cast<ne::time::TimerWheel*>(nullptr)) {}



	void_t Context::Run()
	{
		// Run() 이 실제로 이 줄에 도달하기 전에 다른 스레드가 이미 Stop() 을 호출했다면(예:
		// IoContextPool 이 워커 스레드를 스폰하자마자 곧바로 Stop() 하는 경합), 아래
		// running.store(true) 가 그 Stop() 을 덮어써 이 Run() 이 다시는 깨지 않는 무기한
		// 대기에 빠진다. stopRequested 로 그 경합을 소비해 두면 이번 Run() 은 그냥 즉시 반환한다.
		if (isStopRequested.exchange(false, std::memory_order_acq_rel)) return;

		isRunning.store(true, std::memory_order_release);

		// 무기한 대기 — 완료/타이머/Post(Wake) 중 하나가 루프를 깨운다.
		while (isRunning.load(std::memory_order_acquire))
			(void_t)RunOnce(std::chrono::milliseconds{ -1 });
	}

	bool_t Context::RunOnce(const std::chrono::milliseconds _timeout)
	{
		Completion completions[MaxBatch];
		const int_t count = engine.WaitCompletions(completions, MaxBatch, EffectiveTimeout(_timeout));

		for (int_t i = 0; i < count; ++i)
		{
			auto* handler = static_cast<CompletionHandler*>(completions[i].userData);
			if (handler == nullptr) continue;

			handler->result = completions[i].result;
			handler->isCompleted = true;

			// 프레임이 완료 전 파괴됐으면(abandoned) 소유권이 루프에 넘어온 것 — resume 없이 해제한다.
			// 그 외에는 대기 코루틴을 재개하고, 해제는 awaitable holder 가 담당한다.
			if (handler->isAbandoned)
				delete handler;
			else if (handler->handle && !handler->handle.done())
				handler->handle.resume();
		}

		if (timerWheel != nullptr) timerWheel->Tick();
		DrainPosted();

		return count > 0;
	}

	void_t Context::Post(const std::coroutine_handle<> _handle)
	{
		{
			std::lock_guard lock(postMutex);
			postedHandles.push_back(_handle);
		}

		engine.Wake(); // 대기 중인 루프를 깨워 DrainPosted 가 즉시 돌게 한다
	}

	void_t Context::Stop() noexcept
	{
		// running 이 이미 false 였다면(즉 이 Stop() 이 Run() 보다 먼저 도착) stopRequested 를 세워
		// 그 다음 Run() 진입이 곧바로 반환하게 한다 — 그렇지 않으면 평범한 실행 중 정지이므로
		// running 을 내리는 것만으로 루프가 다음 iteration 에서 빠져나온다.
		if (!isRunning.exchange(false, std::memory_order_acq_rel))
			isStopRequested.store(true, std::memory_order_release);

		engine.Wake();
	}



	std::chrono::milliseconds Context::EffectiveTimeout(const std::chrono::milliseconds _timeout) const noexcept
	{
		if (timerWheel == nullptr) return _timeout;

		const int_t nextExpiry = timerWheel->NextExpiryMs();
		if (nextExpiry < 0) return _timeout; // 예약된 타이머 없음 — 그대로

		// 다음 만료가 더 이르면(또는 요청이 무기한 대기이면) 만료 시점으로 당긴다.
		if (_timeout.count() < 0 || nextExpiry < _timeout.count())
			return std::chrono::milliseconds{ nextExpiry };

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
			if (handle && !handle.done()) handle.resume();
	}

END_NS
