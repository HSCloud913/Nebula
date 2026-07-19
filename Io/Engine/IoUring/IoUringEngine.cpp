//
// Created by hscloud on 26. 6. 30.
//

#include "Io/Engine/IoUring/IoUringEngine.h"

#if defined(IS_POSIX)
#	include <cerrno>
#	include <cstring>
#	include <poll.h>
#	include <sys/eventfd.h>
#	include <sys/sendfile.h>
#	include <sys/socket.h>
#	include <unistd.h>

#	ifndef MSG_ZEROCOPY
#		define MSG_ZEROCOPY 0x4000000
#	endif



BEGIN_NS(ne::io)
	IoUringEngine::IoUringEngine(const uint_t _queueDepth) noexcept
	{
		// io_uring_queue_init 은 커널과 SQ/CQ 링 메모리를 mmap 으로 공유하는 인터페이스를 만든다.
		// 이후 제출/완료는 시스템 콜 없이(또는 io_uring_submit 만으로) 링을 통해 오갈 수 있다.
		if (::io_uring_queue_init(_queueDepth, &ring, 0) != 0) return;

		// io_uring 자체에는 "다른 스레드가 대기를 깨운다" 는 기능이 없으므로, eventfd 를 POLLIN
		// 대상으로 등록해두고 Wake() 가 여기 write 하면 CQE 가 발생해 io_uring_wait_cqe 가 깨어나게 한다.
		wakeEventFd = ::eventfd(0, EFD_NONBLOCK);
		if (wakeEventFd < 0)
		{
			::io_uring_queue_exit(&ring);
			return;
		}

		bufferProvider = std::make_unique<IoUringProvider>(&ring);

		isValid = true;
		ArmWakePoll();
	}

	IoUringEngine::~IoUringEngine()
	{
		if (wakeEventFd >= 0) ::close(wakeEventFd);
		if (isValid) ::io_uring_queue_exit(&ring);
	}



	void_t IoUringEngine::Submit(const Request& _request)
	{
		// io_uring 에는 sendfile 전용 prep 함수가 없어(splice 로 흉내낼 수는 있으나 복잡도가 큼)
		// 여기서만 예외적으로 동기 sendfile() 을 호출하고 결과를 즉시 완료 큐에 넣는다.
		if (_request.requestKind == RequestKind::SEND_FILE)
		{
			off_t offset = static_cast<off_t>(_request.offset);
			const ssize_t bytes = ::sendfile(static_cast<int_t>(_request.handle), static_cast<int_t>(_request.auxHandle), &offset, _request.length);

			std::lock_guard lock(mutex);
			readyCompletions.push_back(Completion{ _request.userData, bytes >= 0 ? static_cast<longlong_t>(bytes) : -static_cast<longlong_t>(errno) });
			return;
		}

		// UringOperation 은 CQE 도착 시까지 살아 있어야 하므로 heap 에 두고, SQE 의 user_data 로
		// 이 포인터를 등록해 완료 시 그대로 복원한다(IocpEngine 의 OVERLAPPED 트릭과 같은 목적).
		auto* operation = new UringOperation{ _request.userData, _request.requestKind, false };

		io_uring_sqe* sqe = AcquireSqe();
		if (sqe == nullptr)
		{
			// SQ 가 가득 차 있고 즉시 제출로도 자리를 못 만든 경우. 커널에 아무 것도 넘기지 않았으므로
			// 안전하게 실패로 확정하고 operation 을 바로 정리한다.
			{
				std::lock_guard lock(mutex);
				readyCompletions.push_back(Completion{ _request.userData, -static_cast<longlong_t>(ENOBUFS) });
			}

			delete operation;
			return;
		}

		const int_t fd = static_cast<int_t>(_request.handle);
		switch (_request.requestKind)
		{
			case RequestKind::ACCEPT:
				::io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0);
				break;
			case RequestKind::CONNECT:
				::io_uring_prep_connect(sqe, fd, static_cast<const sockaddr*>(_request.address), static_cast<socklen_t>(_request.addressLength));
				break;
			case RequestKind::READ:
				if (_request.chain != nullptr)
				{
					operation->iovecs = _request.chain->AsIovec();
					::io_uring_prep_readv(sqe, fd, operation->iovecs.data(), static_cast<uint_t>(operation->iovecs.size()), _request.offset);
				}
				else { ::io_uring_prep_read(sqe, fd, _request.buffer, static_cast<uint_t>(_request.length), _request.offset); }
				break;
			case RequestKind::WRITE:
				if (_request.chain != nullptr)
				{
					operation->iovecs = _request.chain->AsIovec();
					::io_uring_prep_writev(sqe, fd, operation->iovecs.data(), static_cast<uint_t>(operation->iovecs.size()), _request.offset);
				}
				else { ::io_uring_prep_write(sqe, fd, _request.buffer, static_cast<uint_t>(_request.length), _request.offset); }
				break;
			case RequestKind::READ_FIXED:
				// bufferId 는 IoUringProvider::RegisterBuffer() 가 "슬롯 인덱스 + 1" 로 발급하므로
				// 여기서 -1 을 해 원래의 0-based 슬롯 인덱스(buf_index)로 되돌린다.
				::io_uring_prep_read_fixed(sqe, fd, _request.buffer, static_cast<uint_t>(_request.length), _request.offset, static_cast<int_t>(_request.bufferId) - 1);
				break;
			case RequestKind::WRITE_FIXED:
				::io_uring_prep_write_fixed(sqe, fd, _request.buffer, static_cast<uint_t>(_request.length), _request.offset, static_cast<int_t>(_request.bufferId) - 1);
				break;
			case RequestKind::WAIT_READABLE:
				::io_uring_prep_poll_add(sqe, fd, POLLIN);
				break;
			case RequestKind::WAIT_WRITABLE:
				::io_uring_prep_poll_add(sqe, fd, POLLOUT);
				break;
			case RequestKind::RECEIVE:
				if (_request.chain != nullptr)
				{
					operation->iovecs = _request.chain->AsIovec();
					operation->message.msg_iov = operation->iovecs.data();
					operation->message.msg_iovlen = operation->iovecs.size();
					::io_uring_prep_recvmsg(sqe, fd, &operation->message, 0);
				}
				else { ::io_uring_prep_recv(sqe, fd, _request.buffer, _request.length, 0); }
				break;
			case RequestKind::SEND:
				if (_request.chain != nullptr)
				{
					operation->iovecs = _request.chain->AsIovec();
					operation->message.msg_iov = operation->iovecs.data();
					operation->message.msg_iovlen = operation->iovecs.size();
					::io_uring_prep_sendmsg(sqe, fd, &operation->message, 0);
				}
				else { ::io_uring_prep_send(sqe, fd, _request.buffer, _request.length, 0); }
				break;
			case RequestKind::RECEIVE_FROM:
				operation->iovecs = { iovec{ _request.buffer, _request.length } };
				operation->message.msg_iov = operation->iovecs.data();
				operation->message.msg_iovlen = 1;
				operation->message.msg_name = _request.fromAddress;
				operation->message.msg_namelen = _request.fromAddressLength ? static_cast<socklen_t>(*_request.fromAddressLength) : 0;
				operation->fromAddressLength = _request.fromAddressLength;
				::io_uring_prep_recvmsg(sqe, fd, &operation->message, 0);
				break;
			case RequestKind::SEND_TO:
				operation->iovecs = { iovec{ _request.buffer, _request.length } };
				operation->message.msg_iov = operation->iovecs.data();
				operation->message.msg_iovlen = 1;
				operation->message.msg_name = const_cast<void_t*>(_request.address);
				operation->message.msg_namelen = static_cast<socklen_t>(_request.addressLength);
				::io_uring_prep_sendmsg(sqe, fd, &operation->message, 0);
				break;
			case RequestKind::SEND_ZERO_COPY:
				// MSG_ZEROCOPY 는 일반 send 경로에 붙는 리눅스 플래그로, io_uring 전용 zero-copy
				// SQE(IORING_OP_SEND_ZC)를 쓰지 않고도 커널 zero-copy 송신을 유도한다.
				::io_uring_prep_send(sqe, fd, _request.buffer, _request.length, MSG_ZEROCOPY);
				break;
			default:
				::io_uring_prep_nop(sqe);
				operation->isUnsupported = true;
				break;
		}

		::io_uring_sqe_set_data(sqe, operation);

		{
			std::lock_guard lock(mutex);
			// Cancel() 이 이후 이 요청을 찾아 취소할 수 있도록 SQE 를 완전히 채운 뒤 등록한다.
			if (_request.userData != nullptr) inflight[_request.userData] = operation;
		}

		// SQE 를 준비만 해서는 커널이 보지 못한다 - io_uring_submit 으로 커널에 알려야 실제로 처리가 시작된다.
		(void_t)::io_uring_submit(&ring);
	}

	int_t IoUringEngine::WaitCompletions(Completion* _out, const int_t _max, const std::chrono::milliseconds _timeout)
	{
		if (_max <= 0) return 0;

		int_t count = 0;

		// SEND_FILE 처럼 동기로 즉시 확정된 완료가 있으면 CQE 대기 없이 그것부터 반환한다.
		{
			std::lock_guard lock(mutex);
			while (count < _max && !readyCompletions.empty())
			{
				_out[count++] = readyCompletions.back();
				readyCompletions.pop_back();
			}
		}
		if (count > 0) return count;

		// io_uring 링은 이 스레드(루프 스레드)에서만 조작해야 하므로, 다른 스레드가 예약해 둔
		// 취소 SQE 제출을 여기서 대신 수행한다.
		SubmitPendingCancels();

		// io_uring_wait_cqe(_timeout) 로 최소 1개의 CQE 가 준비될 때까지만 블로킹 대기한다.
		io_uring_cqe* firstCqe = nullptr;
		if (_timeout.count() < 0) { (void_t)::io_uring_wait_cqe(&ring, &firstCqe); }
		else
		{
			__kernel_timespec timeSpec{ .tv_sec = _timeout.count() / 1000, .tv_nsec = (_timeout.count() % 1000) * 1'000'000LL };
			if (::io_uring_wait_cqe_timeout(&ring, &firstCqe, &timeSpec) != 0) return 0;
		}

		// 위에서 최소 1개는 이미 준비되었으므로, peek_batch_cqe 로 추가 대기 없이 그 시점에 쌓인
		// CQE 들을 한꺼번에(최대 MaxBatch 개) 배치 회수한다.
		io_uring_cqe* cqes[MaxBatch];
		const uint_t peeked = ::io_uring_peek_batch_cqe(&ring, cqes, static_cast<uint_t>(_max < MaxBatch ? _max : MaxBatch));

		bool_t hasSeenWake = false;
		for (uint_t i = 0; i < peeked; ++i)
		{
			io_uring_cqe* cqe = cqes[i];
			const ulonglong_t userData = ::io_uring_cqe_get_data64(cqe);

			// Wake()/취소용 poll SQE 의 완료는 사용자에게 노출할 실제 I/O 결과가 아니므로 건너뛴다.
			if (userData == WakeUserData)
			{
				hasSeenWake = true;
				continue;
			}

			auto* operation = reinterpret_cast<UringOperation*>(::io_uring_cqe_get_data(cqe));
			if (operation == nullptr) continue;

			// 기본 op 가 아니어서 NOP 로 제출했던 요청은 여기서 EOPNOTSUPP 로 확정한다.
			const longlong_t result = operation->isUnsupported ? -static_cast<longlong_t>(EOPNOTSUPP) : static_cast<longlong_t>(cqe->res);

			// RECEIVE_FROM 처럼 실제 주소 길이를 되돌려줘야 하는 요청은 recvmsg 가 채운 msg_namelen 을 반영한다.
			if (operation->fromAddressLength != nullptr && result >= 0) *operation->fromAddressLength = static_cast<int_t>(operation->message.msg_namelen);

			{
				std::lock_guard lock(mutex);
				if (operation->userData != nullptr) inflight.erase(operation->userData);
			}

			_out[count].userData = operation->userData;
			_out[count].result = result;
			++count;

			delete operation;
		}

		// peek 로 확인한 CQE 들을 커널 CQ 링에서 실제로 소비 처리(consume)한다.
		::io_uring_cq_advance(&ring, peeked);

		if (hasSeenWake)
		{
			// eventfd 카운터를 비워야 다음 Wake() 가 다시 POLLIN 엣지를 만들 수 있고,
			// poll SQE 는 1회성이므로 재무장(ArmWakePoll)해야 다음 Wake() 를 감지할 수 있다.
			uint64_t drained = 0;
			(void_t)::read(wakeEventFd, &drained, sizeof(drained));
			ArmWakePoll();
		}

		return count;
	}

	void_t IoUringEngine::Wake()
	{
		const uint64_t one = 1;
		(void_t)::write(wakeEventFd, &one, sizeof(one));
	}

	void_t IoUringEngine::Cancel(void_t* _userData) noexcept
	{
		if (_userData == nullptr) return;

		// io_uring 링 자체는 단일 스레드 전용이라 여기서 바로 io_uring_prep_cancel 을 호출할 수 없다.
		// 예약만 해두고 Wake() 로 루프 스레드를 깨워 SubmitPendingCancels() 가 대신 처리하게 한다.
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
			case Capability::SEND_FILE_ZERO_COPY:
				return true;
			case Capability::SEND_MEM_ZERO_COPY:
				return true;
			case Capability::RECEIVE_OVERHEAD_REDUCED:
				return true;
			case Capability::RECEIVE_TRUE_ZERO_COPY:
				return false;
		}

		return false;
	}



	io_uring_sqe* IoUringEngine::AcquireSqe() noexcept
	{
		io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
		if (sqe != nullptr) return sqe;

		// SQ 링이 가득 찬 상태 - 이미 채워둔 SQE 들을 먼저 커널에 제출해 슬롯을 비운 뒤 한 번 더 시도한다.
		(void_t)::io_uring_submit(&ring);

		return ::io_uring_get_sqe(&ring);
	}

	void_t IoUringEngine::ArmWakePoll() noexcept
	{
		io_uring_sqe* sqe = AcquireSqe();
		if (sqe == nullptr) return;

		// IORING_OP_POLL_ADD 는 epoll 과 마찬가지로 1회성 알림이라, 완료될 때마다 다시 등록해야
		// 다음 Wake() 신호를 놓치지 않는다. user_data 를 WakeUserData 로 고정해 실제 UringOperation*
		// 값과 절대 겹치지 않게 한다(포인터는 통상 매우 큰 값인 ~0ULL 이 될 수 없음을 전제).
		::io_uring_prep_poll_add(sqe, wakeEventFd, POLLIN);
		::io_uring_sqe_set_data64(sqe, WakeUserData);
		(void_t)::io_uring_submit(&ring);
	}

	void_t IoUringEngine::SubmitPendingCancels() noexcept
	{
		// mutex 를 오래 잡지 않도록 pendingCancels 전체를 스왑해 락 밖에서 순회한다.
		std::vector<void_t*> cancels;
		{
			std::lock_guard lock(mutex);
			cancels.swap(pendingCancels);
		}

		for (void_t* userData : cancels)
		{
			UringOperation* operation = nullptr;
			{
				std::lock_guard lock(mutex);
				if (const auto iterator = inflight.find(userData); iterator != inflight.end()) operation = iterator->second;
			}

			if (operation == nullptr) continue; // 이미 완료되어 inflight 에서 사라진 요청은 취소할 필요가 없다.

			if (io_uring_sqe* sqe = AcquireSqe(); sqe != nullptr)
			{
				// io_uring_prep_cancel 의 두 번째 인자는 취소 대상을 식별하는 값으로, 원본 요청을
				// 제출할 때 user_data 로 등록해 둔 operation 포인터와 동일한 값을 넘겨야 매칭된다.
				::io_uring_prep_cancel(sqe, operation, 0);
				::io_uring_sqe_set_data64(sqe, WakeUserData);
				(void_t)::io_uring_submit(&ring);
			}
		}
	}

END_NS

#endif // IS_POSIX
