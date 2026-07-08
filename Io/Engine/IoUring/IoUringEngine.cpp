//
// Created by hscloud on 26. 6. 30.
//

#include "IoUringEngine.h"

#if defined(IS_POSIX)

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

BEGIN_NS(ne::io)
	IoUringEngine::IoUringEngine(const uint_t _queueDepth) noexcept
	{
		if (::io_uring_queue_init(_queueDepth, &ring, 0) != 0) return;

		wakeEventFd = ::eventfd(0, EFD_NONBLOCK);
		if (wakeEventFd < 0) { ::io_uring_queue_exit(&ring); return; }

		valid = true;
		ArmWakePoll(); // 최초 wake 감시 무장
	}

	IoUringEngine::~IoUringEngine()
	{
		if (wakeEventFd >= 0) ::close(wakeEventFd);
		if (valid) ::io_uring_queue_exit(&ring);
	}

	io_uring_sqe* IoUringEngine::AcquireSqe() noexcept
	{
		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (sqe != nullptr) return sqe;

		// SQ 가 가득 참 — 제출해 비우고 재시도.
		(void)::io_uring_submit(&ring);
		return ::io_uring_get_sqe(&ring);
	}

	void_t IoUringEngine::ArmWakePoll() noexcept
	{
		io_uring_sqe* sqe = AcquireSqe();
		if (sqe == nullptr) return;

		::io_uring_prep_poll_add(sqe, wakeEventFd, POLLIN);
		::io_uring_sqe_set_data64(sqe, WakeUserData);
		(void)::io_uring_submit(&ring);
	}

	void_t IoUringEngine::Submit(const IoRequest& _request)
	{
		auto* operation = new UringOperation{ _request.userData, _request.op, false };

		io_uring_sqe* sqe = AcquireSqe();
		if (sqe == nullptr)
		{
			// SQ 를 비울 수 없음 — 합성 완료로 즉시 오류 반환(ENOBUFS).
			{
				std::lock_guard lock(mutex);
				readyCompletions.push_back(IoCompletion{ _request.userData, -static_cast<longlong_t>(ENOBUFS) });
			}
			delete operation;
			return;
		}

		const int_t fd = static_cast<int_t>(_request.handle);
		switch (_request.op)
		{
		case OpCode::Read:    ::io_uring_prep_read(sqe, fd, _request.buffer, static_cast<uint_t>(_request.length), _request.offset); break;
		case OpCode::Write:   ::io_uring_prep_write(sqe, fd, _request.buffer, static_cast<uint_t>(_request.length), _request.offset); break;
		case OpCode::Receive: ::io_uring_prep_recv(sqe, fd, _request.buffer, _request.length, 0); break;
		case OpCode::Send:    ::io_uring_prep_send(sqe, fd, _request.buffer, _request.length, 0); break;
		case OpCode::Accept:  ::io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0); break;
		case OpCode::Connect: ::io_uring_prep_connect(sqe, fd, static_cast<const sockaddr*>(_request.address), static_cast<socklen_t>(_request.addressLength)); break;
		default:              ::io_uring_prep_nop(sqe); operation->unsupported = true; break; // Level 3.5 op 은 후속 Phase
		}

		::io_uring_sqe_set_data(sqe, operation);

		{
			std::lock_guard lock(mutex);
			if (_request.userData != nullptr) inflight[_request.userData] = operation;
		}

		(void)::io_uring_submit(&ring);
	}

	void_t IoUringEngine::SubmitPendingCancels() noexcept
	{
		std::vector<void*> cancels;
		{
			std::lock_guard lock(mutex);
			cancels.swap(pendingCancels);
		}

		for (void* userData : cancels)
		{
			UringOperation* operation = nullptr;
			{
				std::lock_guard lock(mutex);
				if (const auto iterator = inflight.find(userData); iterator != inflight.end()) operation = iterator->second;
			}
			if (operation == nullptr) continue; // 이미 완료됨

			if (io_uring_sqe* sqe = AcquireSqe(); sqe != nullptr)
			{
				::io_uring_prep_cancel(sqe, operation, 0);
				::io_uring_sqe_set_data64(sqe, WakeUserData); // 취소 자체의 완료는 무시(wake 와 동일 취급)
				(void)::io_uring_submit(&ring);
			}
		}
	}

	int_t IoUringEngine::WaitCompletions(IoCompletion* _out, const int_t _max, const std::chrono::milliseconds _timeout)
	{
		if (_max <= 0) return 0;

		int_t count = 0;

		// 1) 합성 완료(SQ 부족 등) 우선 배출.
		{
			std::lock_guard lock(mutex);
			while (count < _max && !readyCompletions.empty())
			{
				_out[count++] = readyCompletions.back();
				readyCompletions.pop_back();
			}
		}
		if (count > 0) return count;

		// 2) 지연 취소 제출(루프 스레드에서 ring 단일 접근).
		SubmitPendingCancels();

		// 3) 완료 대기.
		io_uring_cqe* firstCqe = nullptr;
		if (_timeout.count() < 0)
		{
			(void)::io_uring_wait_cqe(&ring, &firstCqe);
		}
		else
		{
			__kernel_timespec timeSpec{ .tv_sec = _timeout.count() / 1000, .tv_nsec = (_timeout.count() % 1000) * 1'000'000LL };
			if (::io_uring_wait_cqe_timeout(&ring, &firstCqe, &timeSpec) != 0) return 0; // 타임아웃 등
		}

		// 4) batch 로 회수.
		io_uring_cqe* cqes[MaxBatch];
		const uint_t peeked = ::io_uring_peek_batch_cqe(&ring, cqes, static_cast<uint_t>(_max < MaxBatch ? _max : MaxBatch));

		bool_t sawWake = false;
		for (uint_t i = 0; i < peeked; ++i)
		{
			io_uring_cqe* cqe = cqes[i];
			const ulonglong_t userData = ::io_uring_cqe_get_data64(cqe);

			if (userData == WakeUserData)
			{
				sawWake = true; // wake / 취소 SQE 완료 — 완료로 배출하지 않는다
				continue;
			}

			auto* operation = reinterpret_cast<UringOperation*>(::io_uring_cqe_get_data(cqe));
			if (operation == nullptr) continue;

			const longlong_t result = operation->unsupported ? -static_cast<longlong_t>(EOPNOTSUPP) : static_cast<longlong_t>(cqe->res);

			{
				std::lock_guard lock(mutex);
				if (operation->userData != nullptr) inflight.erase(operation->userData);
			}

			_out[count].userData = operation->userData;
			_out[count].result = result;
			++count;
			delete operation;
		}

		::io_uring_cq_advance(&ring, peeked);

		if (sawWake)
		{
			uint64_t drained = 0;
			(void)::read(wakeEventFd, &drained, sizeof(drained)); // eventfd 비우기
			ArmWakePoll();                                        // 재무장
		}

		return count;
	}

	void_t IoUringEngine::Wake()
	{
		const uint64_t one = 1;
		(void)::write(wakeEventFd, &one, sizeof(one));
	}

	void_t IoUringEngine::Cancel(void* _userData) noexcept
	{
		if (_userData == nullptr) return;

		// ring 은 루프 스레드에서만 만진다 — 취소 요청만 큐잉하고 루프를 깨운다.
		{
			std::lock_guard lock(mutex);
			pendingCancels.push_back(_userData);
		}
		Wake();
	}

	bool_t IoUringEngine::Supports(const Capability _capability) const noexcept
	{
		switch (_capability)
		{
		case Capability::SendFileZeroCopy:    return true;  // splice / SEND_ZC
		case Capability::SendMemZeroCopy:     return true;  // SEND_ZC, Fixed Buffer
		case Capability::RecvOverheadReduced: return true;  // Fixed Buffer
		case Capability::RecvTrueZeroCopy:    return false; // 미구현
		}

		return false;
	}

END_NS

#endif // IS_POSIX
