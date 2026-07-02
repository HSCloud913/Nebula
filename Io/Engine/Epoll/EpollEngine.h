//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#if defined(IS_POSIX)

#include <unordered_map>
#include "Engine/IIoEngine.h"
#include "Handle.h"

BEGIN_NS(ne::io)
	class EpollEngine final :public IIoEngine
	{
	public:
		EpollEngine();
		virtual ~EpollEngine() override = default;

		NEBULA_NON_COPYABLE_MOVABLE(EpollEngine)

	private: // epoll_wait 한 번에 가져올 수 있는 최대 이벤트 개수.
		static constexpr int MaxEvents = 64;

	private:
		using EpollFdHandle = ne::Handle<int_t, decltype([](const int_t _fd) { ::close(_fd); }), -1>;

		EpollFdHandle epollFd;
		std::unordered_map<socket_t, WatchSlots> watches; // fd 별 Read/Write 방향 독립 감시
		ne::time::TimerWheel* timerWheel{ nullptr };

	public: // Socket Reactor
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Watch(socket_t _fd, uint32_t _events, IoCallback _callback) override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Unwatch(socket_t _fd, uint32_t _events = IoEvent::Read | IoEvent::Write) override;

	public: // Socket Proactor — 내부적으로 epoll watch + recv/send 로 에뮬레이션
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitSend(socket_t _fd, const void* _buffer, std::size_t _length, IoContext* _context) noexcept override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitReceive(socket_t _fd, void* _buffer, std::size_t _length, IoContext* _context) noexcept override;

	public: // Common
		[[nodiscard]] virtual ne::Result<void, ne::OsError> RunOnce(ne::int_t _timeoutMs = -1) override;
		virtual void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept override { timerWheel = _wheel; }

	private: // 이벤트 변환
		static uint32_t ToEpollEvents(uint32_t _events) noexcept;
		static uint32_t FromEpollEvents(uint32_t _events) noexcept;
		static uint32_t CombinedInterest(const WatchSlots& _slots) noexcept; // read/write 슬롯 중 활성(callback 존재)인 쪽의 이벤트만 모아 epoll_ctl 에 넘길 마스크를 만든다.

	public:
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(epollFd); }
	};

END_NS

#endif // IS_POSIX
