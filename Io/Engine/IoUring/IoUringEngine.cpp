//
// Created by hscloud on 26. 6. 30.
//

#include "IoUringEngine.h"

#if defined(IS_POSIX)
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#include "Timer/TimerWheel.h"



BEGIN_NS (ne::io)
	IoUringEngine::IoUringEngine(const unsigned _queueDepth) noexcept
	{
		if (::io_uring_queue_init(_queueDepth, &ring, 0) < 0) return;
		isValid = true;
	}

	IoUringEngine::~IoUringEngine()
	{
		if (!isValid) return;
		::io_uring_queue_exit(&ring);
	}



	ne::Result<void, ne::OsError> IoUringEngine::Watch(const socket_t _fd, const uint32_t _events, IoCallback _callback)
	{
		// fd 하나에 Read/Write 를 독립된 슬롯으로 보관 — 한 방향의 재등록이 반대 방향의
		// POLL_ADD 를 건드리지 않는다.
		const bool_t isWrite = (_events & IoEvent::Write) != 0;
		WatchSlots& slots = watches[_fd];
		WatchEntry& entry = slots.Slot(_events);

		if (entry.callback)
		{
			// 이 방향에 이미 POLL_ADD 가 걸려있음 — 먼저 제거 요청(best-effort).
			// 오래된 CQE 는 ProcessCqe 에서 generation 불일치로 무시된다.
			if (io_uring_sqe* removeSqe = ::io_uring_get_sqe(&ring))
			{
				::io_uring_prep_poll_remove(removeSqe, MakePollUserData(_fd, isWrite, entry.generation));
				::io_uring_sqe_set_data64(removeSqe, 0ULL);
			}

			entry = WatchEntry{};
		}

		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (!sqe)
		{
			if (slots.Empty()) watches.erase(_fd);
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ EBUSY }.Context("[IoUringEngine/Watch] SQ ring full"));
		}

		entry.generation = nextGeneration++;
		entry.events = _events;
		entry.callback = std::move(_callback);

		const uint64_t userData = MakePollUserData(_fd, isWrite, entry.generation);
		::io_uring_prep_poll_add(sqe, static_cast<int>(_fd), ToPollEvents(_events));
		::io_uring_sqe_set_data64(sqe, userData);

		if (::io_uring_submit(&ring) < 0)
		{
			// 실패 — 커널에 실제로 제출되지 않았으므로 이 세대의 CQE 는 영원히 오지 않는다.
			// 방금 채운 슬롯을 되돌리지 않으면 죽은 콜백(보통 이미 소멸된 Awaitable 을 가리킴)이
			// 다음 Watch()/Unwatch() 로 덮어써질 때까지 맵에 그대로 남는다.
			const int submitErrno = errno;
			entry = WatchEntry{};
			if (slots.Empty()) watches.erase(_fd);

			return ne::Result<void, ne::OsError>::Error(ne::OsError{ submitErrno }.Context("[IoUringEngine/Watch]"));
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IoUringEngine::Unwatch(const socket_t _fd, const uint32_t _events)
	{
		const auto watch = watches.find(_fd);
		if (watch == watches.end())
			return ne::Result<void, ne::OsError>::Ok();

		const auto removeDirection = [this, _fd](WatchEntry& _entry, const bool_t _isWrite)
		{
			if (!_entry.callback) return;

			if (io_uring_sqe* sqe = ::io_uring_get_sqe(&ring))
			{
				::io_uring_prep_poll_remove(sqe, MakePollUserData(_fd, _isWrite, _entry.generation));
				::io_uring_sqe_set_data64(sqe, 0ULL);
				(void)::io_uring_submit(&ring);
			}

			_entry = WatchEntry{};
		};

		if (_events & IoEvent::Read) removeDirection(watch->second.read, false);
		if (_events & IoEvent::Write) removeDirection(watch->second.write, true);

		if (watch->second.Empty()) watches.erase(watch);

		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Result<void, ne::OsError> IoUringEngine::SubmitSend(const socket_t _fd, const void* _buffer, const std::size_t _length, IoContext* _context) noexcept
	{
		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (!sqe)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ EBUSY }.Context("[IoUringEngine/SubmitSend] SQ ring full"));

		::io_uring_prep_send(sqe, static_cast<int>(_fd), _buffer, _length, MSG_NOSIGNAL);
		::io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(_context));

		if (::io_uring_submit(&ring) < 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[IoUringEngine/SubmitSend]"));

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IoUringEngine::SubmitReceive(const socket_t _fd, void* _buffer, const std::size_t _length, IoContext* _context) noexcept
	{
		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (!sqe)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ EBUSY }.Context("[IoUringEngine/SubmitReceive] SQ ring full"));

		::io_uring_prep_recv(sqe, static_cast<int>(_fd), _buffer, _length, 0);
		::io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(_context));

		if (::io_uring_submit(&ring) < 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[IoUringEngine/SubmitReceive]"));

		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Result<void, ne::OsError> IoUringEngine::RunOnce(const ne::int_t _timeoutMs)
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

		(void)::io_uring_submit(&ring);

		io_uring_cqe* firstCqe = nullptr;
		int result;
		if (effectiveTimeout < 0)
		{
			result = ::io_uring_wait_cqe(&ring, &firstCqe);
		}
		else
		{
			__kernel_timespec ts{
				.tv_sec = effectiveTimeout / 1000,
				.tv_nsec = (effectiveTimeout % 1000) * 1'000'000LL,
			};
			result = ::io_uring_wait_cqe_timeout(&ring, &firstCqe, &ts);
		}

		if (result == -EINTR || result == -ETIME || result == -EAGAIN)
		{
			if (timerWheel) timerWheel->Tick();
			return ne::Result<void, ne::OsError>::Ok();
		}

		if (result < 0)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ -result }.Context("[IoUringEngine/RunOnce]"));

		io_uring_cqe* cqes[MaxCqeBatch];

		const unsigned count = ::io_uring_peek_batch_cqe(&ring, cqes, MaxCqeBatch);
		for (unsigned i = 0; i < count; ++i)
		{
			ProcessCqe(cqes[i]);
		}

		::io_uring_cq_advance(&ring, count);

		if (timerWheel) timerWheel->Tick();

		return ne::Result<void, ne::OsError>::Ok();
	}



	// ── Proactor: 파일 I/O (IIoEngine 외부 — AsyncFile 전용) ─────────────────

	ne::Result<void, ne::OsError> IoUringEngine::SubmitRead(const int _fd, void* _buffer, const std::size_t _length, const std::size_t _offset, IoContext* _context) noexcept
	{
		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (!sqe)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ EBUSY }.Context("[IoUringEngine/SubmitRead] SQ ring full"));

		::io_uring_prep_read(sqe, _fd, _buffer, static_cast<unsigned>(_length), static_cast<uint64_t>(_offset));
		::io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(_context));

		if (::io_uring_submit(&ring) < 0)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[IoUringEngine/SubmitRead]"));

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IoUringEngine::SubmitWrite(const int _fd, const void* _buffer, const std::size_t _length, const std::size_t _offset, IoContext* _context) noexcept
	{
		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (!sqe)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ EBUSY }.Context("[IoUringEngine/SubmitWrite] SQ ring full"));

		::io_uring_prep_write(sqe, _fd, _buffer, static_cast<unsigned>(_length), static_cast<uint64_t>(_offset));
		::io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(_context));

		if (::io_uring_submit(&ring) < 0)
			return ne::Result<void, ne::OsError>::Error(ne::OsError{ errno }.Context("[IoUringEngine/SubmitWrite]"));

		return ne::Result<void, ne::OsError>::Ok();
	}



	void IoUringEngine::ProcessCqe(io_uring_cqe* _cqe) noexcept
	{
		const uint64_t userData = ::io_uring_cqe_get_data64(_cqe);
		if (userData == 0ULL) return; // NOP (POLL_REMOVE CQE 또는 종료 신호)

		const int result = _cqe->res;
		if (IsSocketPoll(userData))
		{
			// 소켓 POLL_ADD 완료 → 콜백 디스패치
			const socket_t fd = GetPollFd(userData);
			const uint32_t gen = GetPollGen(userData);
			const bool_t isWrite = IsWriteDir(userData);

			const auto watch = watches.find(fd);
			if (watch == watches.end()) return;

			WatchEntry& entry = isWrite ? watch->second.write : watch->second.read;
			// generation 불일치: Watch()/Unwatch() 로 이미 대체/제거된 이전 세대의 CQE.
			// callback 없음: 방어적 가드 — 위 조건과 함께라면 이 시점엔 항상 값이 있어야 함.
			if (entry.generation != gen || !entry.callback) return;

			const uint32_t events = (result < 0) ? IoEvent::Error : FromPollEvents(static_cast<uint32_t>(result));

			IoCallback callback = std::move(entry.callback);
			entry = WatchEntry{};
			if (watch->second.Empty()) watches.erase(watch);

			callback(fd, events);
		}
		else
		{
			// IoContext* — 소켓 proactor (RECV/SEND) 또는 파일 proactor (READ/WRITE)
			auto* context = reinterpret_cast<IoContext*>(userData);
			if (result < 0)
			{
				context->result = ne::Result<std::size_t, ne::OsError>::Error(
					ne::OsError{ static_cast<ne::ulong_t>(-result) });
			}
			else
			{
				context->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(result));
			}

			if (context->handle && !context->handle.done()) context->handle.resume();
		}
	}


	// user_data 인코딩/디코딩 — 레이아웃: [63] SocketPollTag | [62] WriteDirTag | [32-61] generation (30비트) | [0-31] fd (32비트)

	uint64_t IoUringEngine::MakePollUserData(const socket_t _fd, const bool_t _isWrite, const uint32_t _gen) noexcept
	{
		return SocketPollTag
				| (_isWrite ? WriteDirTag : 0ULL)
				| (static_cast<uint64_t>(_gen & 0x3FFF'FFFFu) << 32)
				| static_cast<uint64_t>(static_cast<uint32_t>(_fd));
	}


	uint32_t IoUringEngine::ToPollEvents(const uint32_t _events) noexcept
	{
		uint32_t result = 0;
		if (_events & IoEvent::Read) result |= static_cast<uint32_t>(POLLIN);
		if (_events & IoEvent::Write) result |= static_cast<uint32_t>(POLLOUT);
		if (_events & IoEvent::Error) result |= static_cast<uint32_t>(POLLERR);
		if (_events & IoEvent::HangUp) result |= static_cast<uint32_t>(POLLHUP);

		return result;
	}

	uint32_t IoUringEngine::FromPollEvents(const uint32_t _events) noexcept
	{
		uint32_t result = 0;
		if (_events & static_cast<uint32_t>(POLLIN)) result |= IoEvent::Read;
		if (_events & static_cast<uint32_t>(POLLOUT)) result |= IoEvent::Write;
		if (_events & static_cast<uint32_t>(POLLERR)) result |= IoEvent::Error;
		if (_events & static_cast<uint32_t>(POLLHUP)) result |= IoEvent::HangUp;

		return result;
	}

END_NS

#endif // IS_POSIX
