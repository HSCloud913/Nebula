//
// Created by hscloud on 26. 7. 8.
//

#include "IoContext.h"

#include <limits>
#include "Timer/TimerWheel.h"

BEGIN_NS(ne::io)
	IoContext::IoContext(IIoEngine& _engine) noexcept
		: engine(_engine)
		, timerWheel(static_cast<ne::time::TimerWheel*>(nullptr)) {}

	void_t IoContext::SetTimerWheel(ne::time::TimerWheel* _timerWheel) noexcept
	{
		timerWheel = _timerWheel;
	}

	std::chrono::milliseconds IoContext::EffectiveTimeout(const std::chrono::milliseconds _timeout) const noexcept
	{
		if (timerWheel == nullptr) return _timeout;

		const int_t nextExpiry = timerWheel->NextExpiryMs();
		if (nextExpiry < 0) return _timeout; // 예약된 타이머 없음 — 그대로

		// 다음 만료가 더 이르면(또는 요청이 무기한 대기이면) 만료 시점으로 당긴다.
		if (_timeout.count() < 0 || nextExpiry < _timeout.count())
			return std::chrono::milliseconds{ nextExpiry };

		return _timeout;
	}

	bool_t IoContext::RunOnce(const std::chrono::milliseconds _timeout)
	{
		IoCompletion completions[MaxBatch];
		const int_t count = engine.WaitCompletions(completions, MaxBatch, EffectiveTimeout(_timeout));

		for (int_t i = 0; i < count; ++i)
		{
			auto* handler = static_cast<IoCompletionHandler*>(completions[i].userData);
			if (handler == nullptr) continue;

			handler->result = completions[i].result;
			handler->completed = true;

			// 프레임이 완료 전 파괴됐으면(abandoned) 소유권이 루프에 넘어온 것 — resume 없이 해제한다.
			// 그 외에는 대기 코루틴을 재개하고, 해제는 awaitable holder 가 담당한다.
			if (handler->abandoned)
				delete handler;
			else if (handler->handle && !handler->handle.done())
				handler->handle.resume();
		}

		if (timerWheel != nullptr) timerWheel->Tick();
		DrainPosted();

		return count > 0;
	}

	void_t IoContext::Run()
	{
		running.store(true, std::memory_order_release);

		// 무기한 대기 — 완료/타이머/Post(Wake) 중 하나가 루프를 깨운다.
		while (running.load(std::memory_order_acquire))
			(void)RunOnce(std::chrono::milliseconds{ -1 });
	}

	void_t IoContext::Post(const std::coroutine_handle<> _handle)
	{
		{
			std::lock_guard lock(postMutex);
			postedHandles.push_back(_handle);
		}

		engine.Wake(); // 대기 중인 루프를 깨워 DrainPosted 가 즉시 돌게 한다
	}

	void_t IoContext::Stop() noexcept
	{
		running.store(false, std::memory_order_release);
		engine.Wake();
	}

	void_t IoContext::DrainPosted()
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
