//
// Created by hscloud on 26. 7. 8.
//

#include "Io/Context/Context.h"

#include <cassert>
#include "Time/Timer/TimerWheel.h"



BEGIN_NS(ne::io)
	Context::Context(IEngine& _engine, ne::time::TimerWheel* _timerWheel) noexcept
		: engine(_engine)
		, timerWheel(_timerWheel) {}



	void_t Context::Start()
	{
		if (isStopRequested.exchange(false, std::memory_order_acq_rel)) return;

		isRunning.store(true, std::memory_order_release);

		while (isRunning.load(std::memory_order_acquire)) (void_t)RunOnce(std::chrono::milliseconds{ -1 });
	}

	void_t Context::Stop() noexcept
	{
		if (!isRunning.exchange(false, std::memory_order_acq_rel)) isStopRequested.store(true, std::memory_order_release);

		engine.Wake();
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

			if (handler->isAbandoned) delete handler;
			else if (handler->handle && !handler->handle.done()) handler->handle.resume();
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

		engine.Wake();
	}



	ne::time::Awaitable Context::SleepFor(const std::chrono::milliseconds _duration) const noexcept
	{
		assert(timerWheel != nullptr && "Context::SleepFor requires SetTimerWheel() before use");
		return ne::time::SleepFor(*timerWheel, _duration);
	}



	std::chrono::milliseconds Context::EffectiveTimeout(const std::chrono::milliseconds _timeout) const noexcept
	{
		if (timerWheel == nullptr) return _timeout;

		const int_t nextExpiry = timerWheel->NextExpiryMs();
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

END_NS
