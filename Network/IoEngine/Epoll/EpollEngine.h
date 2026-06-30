//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#if defined(IS_POSIX)

#include <unordered_map>
#include "IIoEngine.h"
#include "NebulaHandle.h"

BEGIN_NS (ne::network)
	class EpollEngine final :public IIoEngine
	{
		NEBULA_NON_COPYABLE_MOVABLE(EpollEngine)

	public:
		EpollEngine();
		~EpollEngine() override = default;

	private:
		struct WatchEntry
		{
			uint32_t events{};
			IoCallback callback;
		};

		using EpollFdHandle = ne::Handle<
			int_t,
			decltype([](const int_t _fd) { ::close(_fd); }),
			-1
		>;

		EpollFdHandle epollFd;
		std::unordered_map<socket_t, WatchEntry> watches;
		ne::time::TimerWheel* timerWheel{ nullptr };

	public:
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Watch(socket_t _fd, uint32_t _events, IoCallback _cb) override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Unwatch(socket_t _fd) override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> RunOnce(int_t _timeoutMs = -1) override;
		void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept override { timerWheel = _wheel; }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(epollFd); }

	private:
		static uint32_t ToEpollEvents(uint32_t _events) noexcept;
		static uint32_t FromEpollEvents(uint32_t _events) noexcept;
	};

END_NS

#endif // IS_POSIX
