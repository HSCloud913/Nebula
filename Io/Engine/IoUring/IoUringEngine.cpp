//
// Created by hscloud on 26. 6. 30.
//

#if defined(IS_POSIX)
#include "IoUringEngine.h"
#include <cerrno>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include "TimerWheel.h"

BEGIN_NS(ne::io)
	static constexpr int MaxEpollEvents = 64;



	IoUringEngine::IoUringEngine(const unsigned _queueDepth) noexcept
	{
		if (::io_uring_queue_init(_queueDepth, &ring, 0) < 0)
			return;

		epollFd = EpollFdHandle(::epoll_create1(EPOLL_CLOEXEC));
		if (!epollFd)
		{
			::io_uring_queue_exit(&ring);
			return;
		}

		completionEventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (completionEventFd < 0)
		{
			::io_uring_queue_exit(&ring);
			return;
		}

		// completionEventFd 를 epollFd 에 등록 — io_uring 완료 신호 수신용
		epoll_event ev{};
		ev.events  = EPOLLIN;
		ev.data.fd = completionEventFd;
		if (::epoll_ctl(epollFd.Get(), EPOLL_CTL_ADD, completionEventFd, &ev) < 0)
		{
			::close(completionEventFd);
			completionEventFd = -1;
			::io_uring_queue_exit(&ring);
			return;
		}

		valid = true;
		running.store(true, std::memory_order_relaxed);
		thread = std::thread(&IoUringEngine::ThreadLoop, this);
	}

	IoUringEngine::~IoUringEngine()
	{
		if (!valid) return;

		running.store(false, std::memory_order_release);

		// NOP SQE 로 io_uring_wait_cqe_timeout 을 깨워 ThreadLoop 종료 유도
		if (auto* sqe = ::io_uring_get_sqe(&ring))
		{
			::io_uring_prep_nop(sqe);
			::io_uring_sqe_set_data64(sqe, 0ULL);
			(void)::io_uring_submit(&ring);
		}

		if (thread.joinable()) thread.join();

		::close(completionEventFd);
		::io_uring_queue_exit(&ring);
	}



	// ── Proactor (파일 I/O) ───────────────────────────────────────────────────

	ne::Result<void, ne::OsError> IoUringEngine::SubmitRead(
		const int _fd, void* _buf, const std::size_t _len, const std::size_t _offset, FileIoCtx* _ctx) noexcept
	{
		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (!sqe)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ EBUSY }.Context("[IoUringEngine/SubmitRead] SQ ring full"));

		::io_uring_prep_read(sqe, _fd, _buf, static_cast<unsigned>(_len), static_cast<uint64_t>(_offset));
		::io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(_ctx));

		if (::io_uring_submit(&ring) < 0)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[IoUringEngine/SubmitRead]"));

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IoUringEngine::SubmitWrite(
		const int _fd, const void* _buf, const std::size_t _len, const std::size_t _offset, FileIoCtx* _ctx) noexcept
	{
		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (!sqe)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ EBUSY }.Context("[IoUringEngine/SubmitWrite] SQ ring full"));

		::io_uring_prep_write(sqe, _fd, _buf, static_cast<unsigned>(_len), static_cast<uint64_t>(_offset));
		::io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(_ctx));

		if (::io_uring_submit(&ring) < 0)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[IoUringEngine/SubmitWrite]"));

		return ne::Result<void, ne::OsError>::Ok();
	}



	// ── IIoEngine: Reactor (소켓 이벤트) ─────────────────────────────────────

	ne::Result<void, ne::OsError> IoUringEngine::Watch(const socket_t _fd, const uint32_t _events, IoCallback _cb)
	{
		epoll_event ev{};
		ev.events  = ToEpollEvents(_events);
		ev.data.fd = static_cast<int>(_fd);

		const bool_t alreadyWatched = watches.contains(_fd);
		const int    op             = alreadyWatched ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

		if (::epoll_ctl(epollFd.Get(), op, static_cast<int>(_fd), &ev) == -1)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[IoUringEngine/Watch]"));

		watches[_fd] = { _events, std::move(_cb) };
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IoUringEngine::Unwatch(const socket_t _fd)
	{
		if (!watches.contains(_fd))
			return ne::Result<void, ne::OsError>::Ok();

		if (::epoll_ctl(epollFd.Get(), EPOLL_CTL_DEL, static_cast<int>(_fd), nullptr) == -1)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[IoUringEngine/Unwatch]"));

		watches.erase(_fd);
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IoUringEngine::RunOnce(const ne::int_t _timeoutMs)
	{
		ne::int_t effectiveTimeout = _timeoutMs;
		if (timerWheel)
		{
			const ne::int_t nextExpiry = timerWheel->NextExpiryMs();
			if (nextExpiry >= 0 && (effectiveTimeout < 0 || nextExpiry < effectiveTimeout))
				effectiveTimeout = nextExpiry;
		}

		epoll_event events[MaxEpollEvents];
		const int count = ::epoll_wait(epollFd.Get(), events, MaxEpollEvents, effectiveTimeout);

		if (count == -1)
		{
			if (errno == EINTR)
			{
				if (timerWheel) timerWheel->Tick();
				return ne::Result<void, ne::OsError>::Ok();  // 시그널 인터럽트 — 정상
			}
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[IoUringEngine/RunOnce]"));
		}

		for (int i = 0; i < count; ++i)
		{
			const int fd = events[i].data.fd;

			if (fd == completionEventFd)
			{
				// io_uring 파일 완료 신호 — 이 스레드에서 handle.resume() 호출
				DrainCompletions();
			}
			else
			{
				// 소켓 이벤트 — 등록된 콜백 디스패치
				const auto evts = FromEpollEvents(events[i].events);
				if (const auto it = watches.find(static_cast<socket_t>(fd)); it != watches.end())
					it->second.callback(static_cast<socket_t>(fd), evts);
			}
		}

		if (timerWheel) timerWheel->Tick();
		return ne::Result<void, ne::OsError>::Ok();
	}



	// ── 내부 구현 ─────────────────────────────────────────────────────────────

	void IoUringEngine::ThreadLoop()
	{
		while (running.load(std::memory_order_acquire))
		{
			io_uring_cqe* cqe = nullptr;
			__kernel_timespec ts{
				.tv_sec  = 0,
				.tv_nsec = 100'000'000LL,  // 100ms — running 플래그 주기적 확인
			};
			const int ret = ::io_uring_wait_cqe_timeout(&ring, &cqe, &ts);

			if (ret == -EAGAIN || ret == -ETIME) continue;
			if (ret < 0 || !cqe) continue;

			const uint64_t userData = ::io_uring_cqe_get_data64(cqe);
			const int      res      = cqe->res;
			::io_uring_cqe_seen(&ring, cqe);

			if (userData == 0ULL) continue;  // NOP (종료 신호)

			auto* ctx = reinterpret_cast<FileIoCtx*>(userData);
			if (res < 0)
				ctx->result = ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ -res });
			else
				ctx->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(res));

			// RunOnce 스레드로 전달 — handle.resume() 은 RunOnce 에서 호출
			completionQueue.Enqueue(ctx->handle);
			uint64_t val = 1;
			(void)::write(completionEventFd, &val, sizeof(val));
		}
	}

	void IoUringEngine::DrainCompletions() noexcept
	{
		// eventfd 카운터 소비
		uint64_t val{};
		(void)::read(completionEventFd, &val, sizeof(val));

		std::coroutine_handle<> handle;
		while (completionQueue.Dequeue(handle))
		{
			if (handle && !handle.done())
				handle.resume();
		}
	}



	uint32_t IoUringEngine::ToEpollEvents(const uint32_t _events) noexcept
	{
		uint32_t result = 0;
		if (_events & IoEvent::Read)   result |= static_cast<uint32_t>(EPOLLIN);
		if (_events & IoEvent::Write)  result |= static_cast<uint32_t>(EPOLLOUT);
		if (_events & IoEvent::Error)  result |= static_cast<uint32_t>(EPOLLERR);
		if (_events & IoEvent::HangUp) result |= static_cast<uint32_t>(EPOLLHUP);
		return result;
	}

	uint32_t IoUringEngine::FromEpollEvents(const uint32_t _events) noexcept
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
