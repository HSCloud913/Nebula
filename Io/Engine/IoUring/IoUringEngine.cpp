//
// Created by hscloud on 26. 6. 30.
//

#include "IoUringEngine.h"

#if defined(IS_POSIX)
#	include <cerrno>
#	include <cstring>
#	include <poll.h>
#	include <sys/eventfd.h>
#	include <sys/sendfile.h>
#	include <sys/socket.h>
#	include <unistd.h>

#	ifndef MSG_ZEROCOPY
#		define MSG_ZEROCOPY 0x4000000 // 커널 4.14+ (linux/socket.h) — 오래된 헤더 대비 폴백 정의
#	endif



BEGIN_NS(ne::io)
	IoUringEngine::IoUringEngine(const uint_t _queueDepth) noexcept
	{
		if (::io_uring_queue_init(_queueDepth, &ring, 0) != 0) return;

		wakeEventFd = ::eventfd(0, EFD_NONBLOCK);
		if (wakeEventFd < 0) { ::io_uring_queue_exit(&ring); return; }

		bufferProvider = std::make_unique<IoUringProvider>(&ring);

		isValid = true;
		ArmWakePoll(); // 최초 wake 감시 무장
	}

	IoUringEngine::~IoUringEngine()
	{
		if (wakeEventFd >= 0) ::close(wakeEventFd);
		if (isValid) ::io_uring_queue_exit(&ring);
	}



	void_t IoUringEngine::Submit(const IoRequest& _request)
	{
		// SendFile — io_uring SQE 를 쓰지 않고 동기 sendfile(2) 로 즉시 처리한다(단순화: splice
		// 체인 기반 진짜 비동기 구현은 후속 과제). handle=목적지 소켓(Send 계열과 동일), auxHandle=원본 파일.
		if (_request.op == OpCode::SendFile)
		{
			off_t offset = static_cast<off_t>(_request.offset);
			const ssize_t bytes = ::sendfile(static_cast<int_t>(_request.handle), static_cast<int_t>(_request.auxHandle), &offset, _request.length);

			std::lock_guard lock(mutex);
			readyCompletions.push_back(IoCompletion{ _request.userData, bytes >= 0 ? static_cast<longlong_t>(bytes) : -static_cast<longlong_t>(errno) });
			return;
		}

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
		case OpCode::Read:
			if (_request.chain != nullptr)
			{
				operation->iovecs = _request.chain->AsIovec();
				::io_uring_prep_readv(sqe, fd, operation->iovecs.data(), static_cast<uint_t>(operation->iovecs.size()), _request.offset);
			}
			else
			{
				::io_uring_prep_read(sqe, fd, _request.buffer, static_cast<uint_t>(_request.length), _request.offset);
			}
			break;
		case OpCode::Write:
			if (_request.chain != nullptr)
			{
				operation->iovecs = _request.chain->AsIovec();
				::io_uring_prep_writev(sqe, fd, operation->iovecs.data(), static_cast<uint_t>(operation->iovecs.size()), _request.offset);
			}
			else
			{
				::io_uring_prep_write(sqe, fd, _request.buffer, static_cast<uint_t>(_request.length), _request.offset);
			}
			break;
		case OpCode::Receive:
			if (_request.chain != nullptr)
			{
				operation->iovecs = _request.chain->AsIovec();
				operation->message.msg_iov = operation->iovecs.data();
				operation->message.msg_iovlen = operation->iovecs.size();
				::io_uring_prep_recvmsg(sqe, fd, &operation->message, 0);
			}
			else
			{
				::io_uring_prep_recv(sqe, fd, _request.buffer, _request.length, 0);
			}
			break;
		case OpCode::Send:
			if (_request.chain != nullptr)
			{
				operation->iovecs = _request.chain->AsIovec();
				operation->message.msg_iov = operation->iovecs.data();
				operation->message.msg_iovlen = operation->iovecs.size();
				::io_uring_prep_sendmsg(sqe, fd, &operation->message, 0);
			}
			else
			{
				::io_uring_prep_send(sqe, fd, _request.buffer, _request.length, 0);
			}
			break;
		// 비연결형(UDP) 송수신 — plain send/recv 는 목적지/발신자 주소를 못 실으므로 sendmsg/recvmsg
		// 로 통일한다(msghdr.msg_name 에 주소를 싣는 것 자체는 chain 경로와 동일한 메커니즘).
		case OpCode::SendTo:
			operation->iovecs = { iovec{ _request.buffer, _request.length } };
			operation->message.msg_iov = operation->iovecs.data();
			operation->message.msg_iovlen = 1;
			operation->message.msg_name = const_cast<void*>(_request.address);
			operation->message.msg_namelen = static_cast<socklen_t>(_request.addressLength);
			::io_uring_prep_sendmsg(sqe, fd, &operation->message, 0);
			break;
		case OpCode::ReceiveFrom:
			operation->iovecs = { iovec{ _request.buffer, _request.length } };
			operation->message.msg_iov = operation->iovecs.data();
			operation->message.msg_iovlen = 1;
			operation->message.msg_name = _request.fromAddress;
			operation->message.msg_namelen = _request.fromAddressLength ? static_cast<socklen_t>(*_request.fromAddressLength) : 0;
			operation->fromAddressLength = _request.fromAddressLength; // 완료 후 실채움 길이를 되돌려주기 위해 보관
			::io_uring_prep_recvmsg(sqe, fd, &operation->message, 0);
			break;
		case OpCode::Accept:
			::io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0);
			break;
		case OpCode::Connect:
			::io_uring_prep_connect(sqe, fd, static_cast<const sockaddr*>(_request.address), static_cast<socklen_t>(_request.addressLength));
			break;
		// bufferId 는 IoUringRegisteredBufferProvider::RegisterBuffer 가 돌려준 BufferHandle.value
		// (슬롯 인덱스+1, 0=무효) — buf_index 로 넘길 땐 -1 해서 원래 슬롯 인덱스로 되돌린다.
		// 미리 RegisterBuffer 로 등록된 슬롯이어야 한다.
		case OpCode::ReadFixed:
			::io_uring_prep_read_fixed(sqe, fd, _request.buffer, static_cast<uint_t>(_request.length), _request.offset, static_cast<int_t>(_request.bufferId) - 1);
			break;
		case OpCode::WriteFixed:
			::io_uring_prep_write_fixed(sqe, fd, _request.buffer, static_cast<uint_t>(_request.length), _request.offset, static_cast<int_t>(_request.bufferId) - 1);
			break;
		// MSG_ZEROCOPY — 등록 불필요(opportunistic). 두 번째(zerocopy-complete) CQE 는 이 구현에서
		// 추적하지 않는다(버퍼 재사용 안전성은 호출자 책임) — io_uring_prep_send_zc 의 2-CQE 프로토콜
		// 대신 단순함을 택함.
		case OpCode::SendZeroCopy:
			::io_uring_prep_send(sqe, fd, _request.buffer, _request.length, MSG_ZEROCOPY);
			break;
		default:
			::io_uring_prep_nop(sqe); operation->isUnsupported = true;
			break; // 알 수 없는 op
		}

		::io_uring_sqe_set_data(sqe, operation);

		{
			std::lock_guard lock(mutex);
			if (_request.userData != nullptr) inflight[_request.userData] = operation;
		}

		(void)::io_uring_submit(&ring);
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

			const longlong_t result = operation->isUnsupported ? -static_cast<longlong_t>(EOPNOTSUPP) : static_cast<longlong_t>(cqe->res);

			// ReceiveFrom 완료 — recvmsg 가 채운 실제 발신자 주소 길이를 호출자 포인터로 되돌린다.
			if (operation->fromAddressLength != nullptr && result >= 0)
				*operation->fromAddressLength = static_cast<int_t>(operation->message.msg_namelen);

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
		case Capability::SendFileZeroCopy: return true;  // sendfile(2) 동기 폴백(SendFile)
		case Capability::SendMemZeroCopy: return true;  // MSG_ZEROCOPY(SendZeroCopy)
		case Capability::RecvOverheadReduced: return true;  // Fixed Buffer(ReadFixed/WriteFixed) — 사전 등록 필요
		case Capability::RecvTrueZeroCopy: return false; // 미구현
		}

		return false;
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

END_NS

#endif // IS_POSIX
