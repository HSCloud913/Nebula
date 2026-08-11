//
// Created by hscloud on 26. 7. 28.
//

#pragma once
#include <coroutine>
#include <deque>
#include "Base/Type.h"
#include "Io/Context.h"
#include "Base/Coroutine/Event.h"

namespace ne::network::http_2::internal
{
	/**
	 * @class AsyncMutex
	 * @brief 코루틴용 비재진입 상호배제(단일 스레드 전용).
	 *
	 * 프레임 쓰기를 직렬화해 여러 스트림의 Send 코루틴이 소켓에 동시에 겹쳐 쓰지 않게 합니다
	 * (겹친 쓰기는 프레임 경계를 훼손할 수 있음). Lock() 을 co_await 하고 끝나면 Unlock().
	 */
	class AsyncMutex
	{
	public:
		AsyncMutex() = default;
		NEBULA_NON_COPYABLE_MOVABLE(AsyncMutex)

	private:
		struct LockAwaiter
		{
			AsyncMutex& mutex;

			[[nodiscard]] bool await_ready() const noexcept
			{
				if (!mutex.locked)
				{
					mutex.locked = true;
					return true;
				}
				return false;
			}

			void await_suspend(const std::coroutine_handle<> _handle) const { mutex.waiters.push_back(_handle); }
			void await_resume() const noexcept {}
		};

	private:
		bool_t locked{ false };
		std::deque<std::coroutine_handle<>> waiters;

	public:
		[[nodiscard]] LockAwaiter Lock() noexcept { return LockAwaiter{ *this }; }

		void_t Unlock()
		{
			if (!waiters.empty())
			{
				const auto handle = waiters.front(); // 락 소유권을 다음 대기자에게 그대로 넘김(locked 유지)
				waiters.pop_front();
				handle.resume();
				return;
			}
			locked = false;
		}
	};
}
