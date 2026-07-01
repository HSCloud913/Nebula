//
// Created by hscloud on 25. 6. 29.
//

#include "EpollEngine.h"

#if defined(IS_POSIX)
#include <cerrno>
#include <sys/epoll.h>
#include <sys/socket.h>
#include "Timer/TimerWheel.h"



BEGIN_NS(ne::io)
	static constexpr int MaxEvents = 64;



	EpollEngine::EpollEngine()
		: epollFd(::epoll_create1(EPOLL_CLOEXEC)) {}



	ne::Result<void, ne::OsError> EpollEngine::Watch(const socket_t _fd, const uint32_t _events, IoCallback _cb)
	{
		epoll_event ev{};
		ev.events  = ToEpollEvents(_events);
		ev.data.fd = static_cast<int>(_fd);

		const bool_t alreadyWatched = watches.contains(_fd);
		const int    op             = alreadyWatched ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

		if (::epoll_ctl(epollFd.Get(), op, static_cast<int>(_fd), &ev) == -1)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[EpollEngine/Watch]"));

		watches[_fd] = { _events, std::move(_cb) };
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> EpollEngine::Unwatch(const socket_t _fd)
	{
		if (!watches.contains(_fd))
			return ne::Result<void, ne::OsError>::Ok();

		if (::epoll_ctl(epollFd.Get(), EPOLL_CTL_DEL, static_cast<int>(_fd), nullptr) == -1)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[EpollEngine/Unwatch]"));

		watches.erase(_fd);
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> EpollEngine::RunOnce(const ne::int_t _timeoutMs)
	{
		ne::int_t effectiveTimeout = _timeoutMs;
		if (timerWheel)
		{
			const ne::int_t nextExpiry = timerWheel->NextExpiryMs();
			if (nextExpiry >= 0 && (effectiveTimeout < 0 || nextExpiry < effectiveTimeout))
				effectiveTimeout = nextExpiry;
		}

		epoll_event events[MaxEvents];
		const int count = ::epoll_wait(epollFd.Get(), events, MaxEvents, effectiveTimeout);

		if (count == -1)
		{
			if (errno == EINTR)
			{
				if (timerWheel) timerWheel->Tick();
				return ne::Result<void, ne::OsError>::Ok();  // 시그널 인터럽트 — 정상
			}
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[EpollEngine/RunOnce]"));
		}

		for (int i = 0; i < count; ++i)
		{
			const auto fd   = static_cast<socket_t>(events[i].data.fd);
			const auto evts = FromEpollEvents(events[i].events);

			// 콜백 내부에서 Unwatch 가 호출될 수 있으므로 반복마다 재검색
			if (const auto watch = watches.find(fd); watch != watches.end())
				watch->second.callback(fd, evts);
		}

		if (timerWheel) timerWheel->Tick();
		return ne::Result<void, ne::OsError>::Ok();
	}



	// Proactor 에뮬레이션: epoll Read/Write 이벤트 대기 후 recv/send 를 내부에서 처리.
	// 호출자(PlainStream)는 Proactor 인터페이스만 사용 — 2단계 syscall 은 이 함수 내부에 감춰진다.

	ne::Result<void, ne::OsError> EpollEngine::SubmitRecv(
		const socket_t _fd, void* _buf, const std::size_t _len, IoCtx* _ctx) noexcept
	{
		return Watch(_fd, IoEvent::Read | IoEvent::HangUp,
			[this, _fd, _buf, _len, _ctx](socket_t _f, const uint32_t _events)
			{
				(void)Unwatch(_f);

				if (_events & IoEvent::Error)
				{
					_ctx->result = ne::Result<std::size_t, ne::OsError>::Error(
						ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[EpollEngine/SubmitRecv]"));
				}
				else
				{
					const ssize_t n = ::recv(_fd, _buf, _len, 0);
					if (n < 0)
						_ctx->result = ne::Result<std::size_t, ne::OsError>::Error(
							ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[EpollEngine/SubmitRecv]"));
					else
						_ctx->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(n));
				}

				if (_ctx->handle && !_ctx->handle.done()) _ctx->handle.resume();
			});
	}

	ne::Result<void, ne::OsError> EpollEngine::SubmitSend(
		const socket_t _fd, const void* _buf, const std::size_t _len, IoCtx* _ctx) noexcept
	{
		return Watch(_fd, IoEvent::Write | IoEvent::Error,
			[this, _fd, _buf, _len, _ctx](socket_t _f, const uint32_t _events)
			{
				(void)Unwatch(_f);

				if (_events & IoEvent::Error)
				{
					_ctx->result = ne::Result<std::size_t, ne::OsError>::Error(
						ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[EpollEngine/SubmitSend]"));
				}
				else
				{
					const ssize_t n = ::send(_fd, _buf, _len, MSG_NOSIGNAL);
					if (n < 0)
						_ctx->result = ne::Result<std::size_t, ne::OsError>::Error(
							ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[EpollEngine/SubmitSend]"));
					else
						_ctx->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(n));
				}

				if (_ctx->handle && !_ctx->handle.done()) _ctx->handle.resume();
			});
	}



	uint32_t EpollEngine::ToEpollEvents(const uint32_t _events) noexcept
	{
		uint32_t result = 0;
		if (_events & IoEvent::Read)   result |= static_cast<uint32_t>(EPOLLIN);
		if (_events & IoEvent::Write)  result |= static_cast<uint32_t>(EPOLLOUT);
		if (_events & IoEvent::Error)  result |= static_cast<uint32_t>(EPOLLERR);
		if (_events & IoEvent::HangUp) result |= static_cast<uint32_t>(EPOLLHUP);
		return result;
	}

	uint32_t EpollEngine::FromEpollEvents(const uint32_t _events) noexcept
	{
		uint32_t result = 0;
		if (_events & static_cast<uint32_t>(EPOLLIN))  result |= IoEvent::Read;
		if (_events & static_cast<uint32_t>(EPOLLOUT)) result |= IoEvent::Write;
		if (_events & static_cast<uint32_t>(EPOLLERR)) result |= IoEvent::Error;
		if (_events & static_cast<uint32_t>(EPOLLHUP)) result |= IoEvent::HangUp;
		return result;
	}
END_NS

#endif // IS_POSIX
