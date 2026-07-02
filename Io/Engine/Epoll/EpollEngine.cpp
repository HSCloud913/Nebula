//
// Created by hscloud on 25. 6. 29.
//

#include "EpollEngine.h"

#if defined(IS_POSIX)
#include <cerrno>
#include <sys/epoll.h>
#include <sys/socket.h>
#include "Timer/TimerWheel.h"



BEGIN_NS (ne::io)
	EpollEngine::EpollEngine()
		: epollFd(::epoll_create1(EPOLL_CLOEXEC)) {}



	ne::Result<void, ne::OsError> EpollEngine::Watch(const socket_t _fd, const uint32_t _events, IoCallback _callback)
	{
		// Read/Write 는 fd 하나 안에서 서로 독립된 슬롯이다 — 한쪽을 갱신해도 반대 방향의
		// 콜백/등록 상태는 건드리지 않는다. epoll 자체는 fd 당 interest 마스크가 하나뿐이므로
		// (epoll_ctl 은 ADD/MOD 가 fd 단위) 두 슬롯의 events 를 합쳐(CombinedInterest) 매번
		// 다시 계산해 넘긴다.
		const bool_t alreadyWatched = watches.contains(_fd);

		WatchSlots& slots = watches[_fd];
		WatchEntry& entry = slots.Slot(_events);
		entry.events = _events;
		entry.callback = std::move(_callback);

		epoll_event event{};
		event.events = ToEpollEvents(CombinedInterest(slots));
		event.data.fd = static_cast<int>(_fd);

		const int op = alreadyWatched ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
		if (::epoll_ctl(epollFd.Get(), op, static_cast<int>(_fd), &event) == -1)
		{
			entry = WatchEntry{}; // 실패 — 방금 채운 슬롯 되돌림
			if (slots.Empty()) watches.erase(_fd);

			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[EpollEngine/Watch]"));
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> EpollEngine::Unwatch(const socket_t _fd, const uint32_t _events)
	{
		const auto watch = watches.find(_fd);
		if (watch == watches.end()) return ne::Result<void, ne::OsError>::Ok();

		if (_events & IoEvent::Read) watch->second.read = WatchEntry{};
		if (_events & IoEvent::Write) watch->second.write = WatchEntry{};

		if (watch->second.Empty())
		{
			// epoll_ctl 이 실패해도(예: fd 가 이미 닫혀 EBADF) 로컬 상태는 그대로 정리한다 —
			// 실패했다고 맵에 빈 WatchSlots 를 영구히 남겨두면, fd 값이 재사용됐을 때 새 소켓의
			// Watch 가 "이미 등록됨"으로 오판되는 등 혼선을 일으킬 수 있다.
			const bool_t isFailed = ::epoll_ctl(epollFd.Get(), EPOLL_CTL_DEL, static_cast<int>(_fd), nullptr) == -1;
			const int deleteErrno = errno;

			watches.erase(watch);

			if (isFailed)
				return ne::Result<void, ne::OsError>::Error(ne::OsError{ static_cast<ne::ulong_t>(deleteErrno) }.Context("[EpollEngine/Unwatch]"));

			return ne::Result<void, ne::OsError>::Ok();
		}

		// 반대 방향이 아직 살아있음 — 남은 방향만으로 interest 마스크를 좁혀 MOD.
		epoll_event event{};
		event.events = ToEpollEvents(CombinedInterest(watch->second));
		event.data.fd = static_cast<int>(_fd);

		if (::epoll_ctl(epollFd.Get(), EPOLL_CTL_MOD, static_cast<int>(_fd), &event) == -1)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[EpollEngine/Unwatch]"));

		return ne::Result<void, ne::OsError>::Ok();
	}



	// 소켓 Proactor 에뮬레이션: epoll Read/Write 이벤트 대기 후 recv/send 를 내부에서 처리.
	// 호출자(PlainStream)는 Proactor 인터페이스만 사용 — 2단계 syscall 은 이 함수 내부에 감춰진다.

	ne::Result<void, ne::OsError> EpollEngine::SubmitReceive(const socket_t _fd, void* _buffer, const std::size_t _length, IoContext* _context) noexcept
	{
		return Watch(_fd, IoEvent::Read | IoEvent::HangUp,
					[this, _fd, _buffer, _length, _context](socket_t _triggeredFd, const uint32_t _events)
					{
						(void)Unwatch(_triggeredFd, IoEvent::Read); // Write 방향(동시 SubmitSend)에는 영향 없음

						if (_events & IoEvent::Error)
						{
							_context->result = ne::Result<std::size_t, ne::OsError>::Error(
								ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[EpollEngine/SubmitReceive]"));
						}
						else
						{
							if (const ssize_t received = ::recv(_fd, _buffer, _length, 0); received < 0)
							{
								_context->result = ne::Result<std::size_t, ne::OsError>::Error(
									ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[EpollEngine/SubmitReceive]"));
							}
							else
							{
								_context->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(received));
							}
						}

						if (_context->handle && !_context->handle.done()) _context->handle.resume();
					});
	}

	ne::Result<void, ne::OsError> EpollEngine::SubmitSend(const socket_t _fd, const void* _buffer, const std::size_t _length, IoContext* _context) noexcept
	{
		return Watch(_fd, IoEvent::Write | IoEvent::Error,
					[this, _fd, _buffer, _length, _context](socket_t _triggeredFd, const uint32_t _events)
					{
						(void)Unwatch(_triggeredFd, IoEvent::Write); // Read 방향(동시 SubmitReceive)에는 영향 없음

						if (_events & IoEvent::Error)
						{
							_context->result = ne::Result<std::size_t, ne::OsError>::Error(
								ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[EpollEngine/SubmitSend]"));
						}
						else
						{
							if (const ssize_t sent = ::send(_fd, _buffer, _length, MSG_NOSIGNAL); sent < 0)
							{
								_context->result = ne::Result<std::size_t, ne::OsError>::Error(
									ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[EpollEngine/SubmitSend]"));
							}
							else
							{
								_context->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(sent));
							}
						}

						if (_context->handle && !_context->handle.done()) _context->handle.resume();
					});
	}

	ne::Result<void, ne::OsError> EpollEngine::RunOnce(const ne::int_t _timeoutMs)
	{
		ne::int_t effectiveTimeout = _timeoutMs;
		if (timerWheel)
		{
			const ne::int_t nextExpiry = timerWheel->NextExpiryMs();
			if (nextExpiry >= 0 && (effectiveTimeout < 0 || nextExpiry < effectiveTimeout))
			{
				effectiveTimeout = nextExpiry;
			}
		}

		epoll_event events[MaxEvents];

		const int count = ::epoll_wait(epollFd.Get(), events, MaxEvents, effectiveTimeout);
		if (count == -1)
		{
			if (errno == EINTR)
			{
				if (timerWheel) timerWheel->Tick();
				return ne::Result<void, ne::OsError>::Ok(); // 시그널 인터럽트 — 정상
			}

			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[EpollEngine/RunOnce]"));
		}

		for (int i = 0; i < count; ++i)
		{
			const auto fd = static_cast<socket_t>(events[i].data.fd);
			const auto mappedEvents = FromEpollEvents(events[i].events);

			// read/write 콜백이 서로 독립적으로 fd 를 Unwatch 할 수 있으므로, 콜백 호출
			// 직전마다 매번 다시 조회한다. 각 슬롯은 자신이 등록한 이벤트(entry.events)와
			// 겹칠 때만 fire — 단 Error/HangUp 은 항상 통과시킨다: epoll 은 EPOLLERR/EPOLLHUP 을
			// 구독 여부와 무관하게 항상 보고하므로(man epoll_ctl), 등록 시 HangUp 을 요청하지
			// 않은 슬롯(예: Write|Error 만 요청한 write 슬롯)이라도 순수 HangUp-only 이벤트를
			// 그냥 흘려버리면 그 방향을 기다리는 코루틴이 영원히 깨어나지 못한다.
			static constexpr uint32_t AlwaysDeliver = IoEvent::Error | IoEvent::HangUp;

			if (const auto watch = watches.find(fd); watch != watches.end() && watch->second.read.callback && (mappedEvents & (watch->second.read.events | AlwaysDeliver)))
			{
				watch->second.read.callback(fd, mappedEvents);
			}

			if (const auto watch = watches.find(fd); watch != watches.end() && watch->second.write.callback && (mappedEvents & (watch->second.write.events | AlwaysDeliver)))
			{
				watch->second.write.callback(fd, mappedEvents);
			}
		}

		if (timerWheel) timerWheel->Tick();
		return ne::Result<void, ne::OsError>::Ok();
	}



	uint32_t EpollEngine::ToEpollEvents(const uint32_t _events) noexcept
	{
		uint32_t result = 0;
		if (_events & IoEvent::Read) result |= static_cast<uint32_t>(EPOLLIN);
		if (_events & IoEvent::Write) result |= static_cast<uint32_t>(EPOLLOUT);
		if (_events & IoEvent::Error) result |= static_cast<uint32_t>(EPOLLERR);
		if (_events & IoEvent::HangUp) result |= static_cast<uint32_t>(EPOLLHUP);

		return result;
	}

	uint32_t EpollEngine::FromEpollEvents(const uint32_t _events) noexcept
	{
		uint32_t result = 0;
		if (_events & static_cast<uint32_t>(EPOLLIN)) result |= IoEvent::Read;
		if (_events & static_cast<uint32_t>(EPOLLOUT)) result |= IoEvent::Write;
		if (_events & static_cast<uint32_t>(EPOLLERR)) result |= IoEvent::Error;
		if (_events & static_cast<uint32_t>(EPOLLHUP)) result |= IoEvent::HangUp;

		return result;
	}

	uint32_t EpollEngine::CombinedInterest(const WatchSlots& _slots) noexcept
	{
		uint32_t mask = 0;
		if (_slots.read.callback) mask |= _slots.read.events;
		if (_slots.write.callback) mask |= _slots.write.events;

		return mask;
	}

END_NS

#endif // IS_POSIX
