//
// Created by hscloud on 26. 6. 30.
//

#include "IoUringEngine.h"

#if defined(IS_POSIX)
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#include "Timer/TimerWheel.h"



BEGIN_NS(ne::io)
	static constexpr unsigned kMaxCqeBatch = 64;



	IoUringEngine::IoUringEngine(const unsigned _queueDepth) noexcept
	{
		if (::io_uring_queue_init(_queueDepth, &ring, 0) < 0)
			return;
		valid = true;
	}

	IoUringEngine::~IoUringEngine()
	{
		if (!valid) return;
		::io_uring_queue_exit(&ring);
	}



	// ── Proactor: 파일 I/O (IIoEngine 외부 — AsyncFile 전용) ─────────────────

	ne::Result<void, ne::OsError> IoUringEngine::SubmitRead(
		const int _fd, void* _buf, const std::size_t _len, const std::size_t _offset, IoContext* _ctx) noexcept
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
		const int _fd, const void* _buf, const std::size_t _len, const std::size_t _offset, IoContext* _ctx) noexcept
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



	// ── IIoEngine: Proactor (소켓 RECV/SEND) ───────────────────────────────────

	ne::Result<void, ne::OsError> IoUringEngine::SubmitReceive(
		const socket_t _fd, void* _buf, const std::size_t _len, IoContext* _ctx) noexcept
	{
		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (!sqe)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ EBUSY }.Context("[IoUringEngine/SubmitReceive] SQ ring full"));

		::io_uring_prep_recv(sqe, static_cast<int>(_fd), _buf, _len, 0);
		::io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(_ctx));

		if (::io_uring_submit(&ring) < 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[IoUringEngine/SubmitReceive]"));

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IoUringEngine::SubmitSend(
		const socket_t _fd, const void* _buf, const std::size_t _len, IoContext* _ctx) noexcept
	{
		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (!sqe)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ EBUSY }.Context("[IoUringEngine/SubmitSend] SQ ring full"));

		::io_uring_prep_send(sqe, static_cast<int>(_fd), _buf, _len, MSG_NOSIGNAL);
		::io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(_ctx));

		if (::io_uring_submit(&ring) < 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[IoUringEngine/SubmitSend]"));

		return ne::Result<void, ne::OsError>::Ok();
	}



	// ── IIoEngine: Reactor (POLL_ADD) ────────────────────────────────────────

	ne::Result<void, ne::OsError> IoUringEngine::Watch(const socket_t _fd, const uint32_t _events, IoCallback _cb)
	{
		if (const auto it = watches.find(_fd); it != watches.end())
		{
			if (io_uring_sqe* removeSqe = ::io_uring_get_sqe(&ring))
			{
				::io_uring_prep_poll_remove(removeSqe, MakePollUserData(_fd, it->second.generation));
				::io_uring_sqe_set_data64(removeSqe, 0ULL);
			}
			watches.erase(it);
		}

		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (!sqe)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ EBUSY }.Context("[IoUringEngine/Watch] SQ ring full"));

		const uint32_t gen      = nextGeneration++;
		const uint64_t userData = MakePollUserData(_fd, gen);

		::io_uring_prep_poll_add(sqe, static_cast<int>(_fd), ToPollEvents(_events));
		::io_uring_sqe_set_data64(sqe, userData);

		if (::io_uring_submit(&ring) < 0)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[IoUringEngine/Watch]"));

		watches[_fd] = { gen, _events, std::move(_cb) };
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IoUringEngine::Unwatch(const socket_t _fd)
	{
		const auto it = watches.find(_fd);
		if (it == watches.end())
			return ne::Result<void, ne::OsError>::Ok();

		if (io_uring_sqe* sqe = ::io_uring_get_sqe(&ring))
		{
			::io_uring_prep_poll_remove(sqe, MakePollUserData(_fd, it->second.generation));
			::io_uring_sqe_set_data64(sqe, 0ULL);
			(void)::io_uring_submit(&ring);
		}

		watches.erase(it);
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

		(void)::io_uring_submit(&ring);

		io_uring_cqe* firstCqe = nullptr;
		int           ret;
		if (effectiveTimeout < 0)
		{
			ret = ::io_uring_wait_cqe(&ring, &firstCqe);
		}
		else
		{
			__kernel_timespec ts{
				.tv_sec  = effectiveTimeout / 1000,
				.tv_nsec = (effectiveTimeout % 1000) * 1'000'000LL,
			};
			ret = ::io_uring_wait_cqe_timeout(&ring, &firstCqe, &ts);
		}

		if (ret == -EINTR || ret == -ETIME || ret == -EAGAIN)
		{
			if (timerWheel) timerWheel->Tick();
			return ne::Result<void, ne::OsError>::Ok();
		}
		if (ret < 0)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ -ret }.Context("[IoUringEngine/RunOnce]"));

		io_uring_cqe* cqes[kMaxCqeBatch];
		const unsigned count = ::io_uring_peek_batch_cqe(&ring, cqes, kMaxCqeBatch);
		for (unsigned i = 0; i < count; ++i)
			ProcessCqe(cqes[i]);
		::io_uring_cq_advance(&ring, count);

		if (timerWheel) timerWheel->Tick();
		return ne::Result<void, ne::OsError>::Ok();
	}



	// ── 내부 구현 ─────────────────────────────────────────────────────────────

	void IoUringEngine::ProcessCqe(io_uring_cqe* _cqe) noexcept
	{
		const uint64_t userData = ::io_uring_cqe_get_data64(_cqe);
		const int      res      = _cqe->res;

		if (userData == 0ULL) return; // NOP (POLL_REMOVE CQE 또는 종료 신호)

		if (IsSocketPoll(userData))
		{
			// 소켓 POLL_ADD 완료 → 콜백 디스패치
			const socket_t fd  = GetPollFd(userData);
			const uint32_t gen = GetPollGen(userData);

			const auto it = watches.find(fd);
			if (it == watches.end() || it->second.generation != gen) return;

			const uint32_t events = (res < 0)
				? IoEvent::Error
				: FromPollEvents(static_cast<uint32_t>(res));

			IoCallback cb = std::move(it->second.callback);
			watches.erase(it);
			cb(fd, events);
		}
		else
		{
			// IoContext* — 소켓 proactor (RECV/SEND) 또는 파일 proactor (READ/WRITE)
			auto* ctx = reinterpret_cast<IoContext*>(userData);
			if (res < 0)
				ctx->result = ne::Result<std::size_t, ne::OsError>::Error(
					ne::OsError{ static_cast<ne::ulong_t>(-res) });
			else
				ctx->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(res));

			if (ctx->handle && !ctx->handle.done())
				ctx->handle.resume();
		}
	}



	// ── user_data 인코딩/디코딩 ───────────────────────────────────────────────
	// 레이아웃: [63] kSocketPollTag | [32-62] generation (31비트) | [0-31] fd (32비트)

	uint64_t IoUringEngine::MakePollUserData(const socket_t _fd, const uint32_t _gen) noexcept
	{
		return kSocketPollTag
			| (static_cast<uint64_t>(_gen & 0x7FFF'FFFFu) << 32)
			| static_cast<uint64_t>(static_cast<uint32_t>(_fd));
	}

	socket_t IoUringEngine::GetPollFd(const uint64_t _userData) noexcept
	{
		return static_cast<socket_t>(static_cast<uint32_t>(_userData & 0xFFFF'FFFFu));
	}

	uint32_t IoUringEngine::GetPollGen(const uint64_t _userData) noexcept
	{
		return static_cast<uint32_t>((_userData >> 32) & 0x7FFF'FFFFu);
	}

	bool_t IoUringEngine::IsSocketPoll(const uint64_t _userData) noexcept
	{
		return (_userData & kSocketPollTag) != 0;
	}



	// ── poll 플래그 변환 ──────────────────────────────────────────────────────

	uint32_t IoUringEngine::ToPollEvents(const uint32_t _events) noexcept
	{
		uint32_t result = 0;
		if (_events & IoEvent::Read)   result |= static_cast<uint32_t>(POLLIN);
		if (_events & IoEvent::Write)  result |= static_cast<uint32_t>(POLLOUT);
		if (_events & IoEvent::Error)  result |= static_cast<uint32_t>(POLLERR);
		if (_events & IoEvent::HangUp) result |= static_cast<uint32_t>(POLLHUP);
		return result;
	}

	uint32_t IoUringEngine::FromPollEvents(const uint32_t _events) noexcept
	{
		uint32_t result = 0;
		if (_events & static_cast<uint32_t>(POLLIN))  result |= IoEvent::Read;
		if (_events & static_cast<uint32_t>(POLLOUT)) result |= IoEvent::Write;
		if (_events & static_cast<uint32_t>(POLLERR)) result |= IoEvent::Error;
		if (_events & static_cast<uint32_t>(POLLHUP)) result |= IoEvent::HangUp;
		return result;
	}
END_NS

#endif // IS_POSIX
