//
// Created by hscloud on 26. 7. 1.
//

#pragma once
#include <coroutine>
#include <optional>
#include "Engine/IIoEngine.h"

BEGIN_NS(ne::io)
	// 단일 fd 이벤트를 co_await 로 대기하는 범용 awaitable.
	// await_suspend 에서 Watch 를 등록하고, 콜백에서 Unwatch 후 코루틴을 재개한다.
	class WatchAwaitable
	{
	public:
		WatchAwaitable(IIoEngine& _engine, const io_fd_t _fd, const uint32_t _targetEvents) noexcept
			: engine(_engine)
			, fd(_fd)
			, targetEvents(_targetEvents) {}

	private:
		IIoEngine& engine;
		io_fd_t    fd;
		uint32_t   targetEvents;
		uint32_t   triggeredEvents{};
		std::optional<ne::OsError> watchError;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			auto r = engine.Watch(fd, targetEvents,
				[this, _handle](const io_fd_t _f, const uint32_t _events) mutable
				{
					// targetEvents 로 요청한 방향만 해제 — 같은 fd 에 대한 반대 방향의
					// 독립된 Watch(예: 다른 코루틴의 대기)에는 영향을 주지 않는다.
					(void)engine.Unwatch(_f, targetEvents);
					triggeredEvents = _events;
					_handle.resume();
				});

			if (r.IsError())
			{
				watchError.emplace(std::move(r.Error()));
				return false;
			}
			return true;
		}

		[[nodiscard]] ne::Result<uint32_t, ne::OsError> await_resume() noexcept
		{
			if (watchError)
				return ne::Result<uint32_t, ne::OsError>::Error(std::move(*watchError));
			return ne::Result<uint32_t, ne::OsError>::Ok(triggeredEvents);
		}
	};
END_NS
