//
// Created by hscloud on 25. 6. 29.
//

#include "Io/Engine/Epoll/EpollEngine.h"

#if defined(IS_POSIX)
#	include <cerrno>
#	include <cstring>
#	include <sys/epoll.h>
#	include <sys/eventfd.h>
#	include <sys/socket.h>
#	include <sys/sendfile.h>
#	include <sys/uio.h>
#	include <unistd.h>

#	ifndef MSG_ZEROCOPY
#		define MSG_ZEROCOPY 0x4000000
#	endif



BEGIN_NS(ne::io)
	EpollEngine::EpollEngine() noexcept
	{
		epollFd = ::epoll_create1(0);
		if (epollFd < 0) return;

		// Wake() 는 다른 스레드에서 호출될 수 있으므로 epoll_wait 자체를 깨울 별도의 fd 가 필요하다.
		// eventfd 는 write 한 값을 카운터에 누적하고 read 로 한 번에 비울 수 있어 이 용도에 적합하다.
		wakeEventFd = ::eventfd(0, EFD_NONBLOCK);
		if (wakeEventFd < 0)
		{
			::close(epollFd);
			epollFd = -1;
			return;
		}

		epoll_event event{};
		event.events = EPOLLIN;
		event.data.fd = wakeEventFd;
		if (::epoll_ctl(epollFd, EPOLL_CTL_ADD, wakeEventFd, &event) != 0)
		{
			::close(wakeEventFd);
			::close(epollFd);
			wakeEventFd = -1;
			epollFd = -1;
			return;
		}

		isValid = true;
	}

	EpollEngine::~EpollEngine()
	{
		if (wakeEventFd >= 0) ::close(wakeEventFd);
		if (epollFd >= 0) ::close(epollFd);
	}



	void_t EpollEngine::Submit(const Request& _request)
	{
		// epoll 은 readiness 통지만 하므로, 가능한 요청은 epoll 을 거치지 않고 바로 수행해 지연을 줄인다.
		// (예: 이미 커널 버퍼에 데이터가 있는 recv 는 등록 없이 바로 끝난다)
		if (longlong_t result = 0; Perform(_request, false, result))
		{
			std::lock_guard lock(mutex);
			ready.push_back(Completion{ _request.userData, result });
			return;
		}

		// EAGAIN 등으로 즉시 처리 불가 -> 이 fd 가 준비될 때까지 pending 에 보관하고 epoll 관심 이벤트를 갱신한다.
		const int_t fd = static_cast<int_t>(_request.handle);
		const bool_t isWrite = IsWriteDirection(_request.requestKind);

		std::lock_guard lock(mutex);

		pending[_request.userData] = PendingOperation{ _request, isWrite };
		if (isWrite) writeWaiter[fd] = _request.userData;
		else readWaiter[fd] = _request.userData;

		UpdateEpoll(fd);
	}

	int_t EpollEngine::WaitCompletions(Completion* _out, const int_t _max, const std::chrono::milliseconds _timeout)
	{
		if (_max <= 0) return 0;

		// 이전 호출에서 예약만 되고 아직 통지되지 않은 취소를 먼저 합성 완료로 만든다.
		ProcessCancels();

		{
			std::lock_guard lock(mutex);

			// Submit() 이 즉시 처리해 둔 결과가 있으면 epoll_wait 를 거치지 않고 그대로 반환한다.
			int_t drained = 0;
			while (drained < _max && !ready.empty())
			{
				_out[drained++] = ready.back();
				ready.pop_back();
			}

			if (drained > 0) return drained;
		}

		// epoll_wait 는 한 번의 대기로 최대 MaxEvents 개의 이벤트를 배치 수확한다(반복 syscall 비용 절감).
		epoll_event events[MaxEvents];
		const int_t timeoutMs = _timeout.count() < 0 ? -1 : static_cast<int_t>(_timeout.count());
		const int_t eventCount = ::epoll_wait(epollFd, events, MaxEvents, timeoutMs);
		if (eventCount <= 0) return 0;

		int_t count = 0;
		for (int_t i = 0; i < eventCount && count < _max; ++i)
		{
			const int_t fd = events[i].data.fd;

			// wakeEventFd 는 실제 I/O 대상이 아니라 Wake() 신호이므로 카운터만 비우고 넘어간다.
			if (fd == wakeEventFd)
			{
				uint64_t drained = 0;
				(void_t)::read(wakeEventFd, &drained, sizeof(drained));
				continue;
			}

			// 같은 fd 에 read/write 요청이 동시에 걸려 있을 수 있으므로 두 방향을 모두 검사한다.
			// EPOLLERR/EPOLLHUP 은 어느 방향으로 등록되어 있든 깨워서 에러를 확정짓게 한다.
			for (int_t direction = 0; direction < 2 && count < _max; ++direction)
			{
				const bool_t isWrite = (direction == 1);
				if (isWrite && !(events[i].events & (EPOLLOUT | EPOLLERR | EPOLLHUP))) continue;
				if (!isWrite && !(events[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP))) continue;

				PendingOperation operation;
				bool_t isFound = false;
				{
					std::lock_guard lock(mutex);

					auto& waiter = isWrite ? writeWaiter : readWaiter;
					const auto iterator = waiter.find(fd);
					if (iterator == waiter.end()) continue;

					void_t* userData = iterator->second;

					const auto pendingIterator = pending.find(userData);
					if (pendingIterator == pending.end())
					{
						// waiter 에는 남아 있지만 pending 에는 없는 경우(이미 취소/처리된 뒤 stale) 정리만 하고 건너뛴다.
						waiter.erase(iterator);
						continue;
					}

					operation = pendingIterator->second;
					isFound = true;
				}
				if (!isFound) continue;

				// 재시도(_isRetry=true) 로 실제 수행. 여전히 EAGAIN 이면(false) 다음 알림까지 그대로 대기.
				longlong_t result = 0;
				if (!Perform(operation.request, true, result)) continue;

				{
					std::lock_guard lock(mutex);
					(isWrite ? writeWaiter : readWaiter).erase(fd);
					pending.erase(operation.request.userData);
					UpdateEpoll(fd);
				}

				_out[count].userData = operation.request.userData;
				_out[count].result = result;
				++count;
			}
		}

		return count;
	}

	void_t EpollEngine::Wake()
	{
		const uint64_t one = 1;
		(void_t)::write(wakeEventFd, &one, sizeof(one));
	}

	void_t EpollEngine::Cancel(void_t* _userData) noexcept
	{
		if (_userData == nullptr) return;

		// 실제 취소 처리는 WaitCompletions() 시작 시점의 ProcessCancels() 에서 일괄 수행한다.
		// 여기서는 예약만 하고 Wake() 로 대기 중인 루프를 즉시 깨운다.
		{
			std::lock_guard lock(mutex);
			pendingCancels.push_back(_userData);
		}

		Wake();
	}

	bool_t EpollEngine::Supports(const Capability _capability) const noexcept
	{
		switch (_capability)
		{
			case Capability::SEND_FILE_ZERO_COPY:
				return true;
			case Capability::SEND_MEM_ZERO_COPY:
				return true;
			case Capability::RECEIVE_OVERHEAD_REDUCED:
				return false;
			case Capability::RECEIVE_TRUE_ZERO_COPY:
				return false;
		}

		return false;
	}



	bool_t EpollEngine::Perform(const Request& _request, const bool_t _isRetry, longlong_t& _result) noexcept
	{
		// RequestKind 에 해당하는 POSIX 시스템 콜을 직접 호출해 synthetic completion 을 만든다.
		// 반환값 규약: true = 완료 확정(_result 에 바이트 수 또는 -errno), false = EAGAIN 등으로
		// 아직 준비되지 않아 epoll 대기가 필요함(호출자가 waiter 에 등록).
		const int_t fd = static_cast<int_t>(_request.handle);

		switch (_request.requestKind)
		{
			case RequestKind::ACCEPT:
			{
				const int_t accepted = ::accept(fd, nullptr, nullptr);
				if (accepted >= 0)
				{
					_result = static_cast<longlong_t>(accepted);
					return true;
				}
				break;
			}
			case RequestKind::CONNECT:
			{
				// 재시도(EPOLLOUT 통지 이후) 시점에는 connect() 결과가 아니라 SO_ERROR 로 실제 성공 여부를 확인해야 한다.
				if (_isRetry)
				{
					int_t soError = 0;
					socklen_t length = sizeof(soError);
					(void_t)::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &length);
					_result = (soError == 0) ? 0 : -static_cast<longlong_t>(soError);
					return true;
				}

				if (::connect(fd, static_cast<const sockaddr*>(_request.address), static_cast<socklen_t>(_request.addressLength)) == 0)
				{
					_result = 0;
					return true;
				}

				// non-blocking connect 의 정상 진행 상태. EPOLLOUT 통지를 기다려야 한다.
				if (errno == EINPROGRESS) return false;
				break;
			}
			case RequestKind::READ:
			{
				// scatter-gather 체인이 있으면 벡터 I/O(preadv)로 한 번에 여러 버퍼를 채운다.
				if (_request.chain != nullptr)
				{
					const auto iov = _request.chain->AsIovec();
					const ssize_t bytes = ::preadv(fd, iov.data(), static_cast<int_t>(iov.size()), static_cast<off_t>(_request.offset));
					if (bytes >= 0)
					{
						_result = static_cast<longlong_t>(bytes);
						return true;
					}
					break;
				}

				const ssize_t bytes = ::pread(fd, _request.buffer, _request.length, static_cast<off_t>(_request.offset));
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::WRITE:
			{
				if (_request.chain != nullptr)
				{
					const auto iov = _request.chain->AsIovec();
					const ssize_t bytes = ::pwritev(fd, iov.data(), static_cast<int_t>(iov.size()), static_cast<off_t>(_request.offset));
					if (bytes >= 0)
					{
						_result = static_cast<longlong_t>(bytes);
						return true;
					}
					break;
				}

				const ssize_t bytes = ::pwrite(fd, _request.buffer, _request.length, static_cast<off_t>(_request.offset));
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::READ_FIXED:
			{
				const ssize_t bytes = ::pread(fd, _request.buffer, _request.length, static_cast<off_t>(_request.offset));
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::WRITE_FIXED:
			{
				const ssize_t bytes = ::pwrite(fd, _request.buffer, _request.length, static_cast<off_t>(_request.offset));
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::WAIT_READABLE:
			case RequestKind::WAIT_WRITABLE:
				// 순수 readiness 대기 요청은 항상 최소 한 번은 epoll 을 거치도록 강제한다(즉시 완료로 처리하지 않음).
				if (!_isRetry) return false;
				_result = 0;
				return true;

			case RequestKind::RECEIVE:
			{
				if (_request.chain != nullptr)
				{
					auto iov = _request.chain->AsIovec();

					msghdr message{};
					message.msg_iov = iov.data();
					message.msg_iovlen = iov.size();

					const ssize_t bytes = ::recvmsg(fd, &message, 0);
					if (bytes >= 0)
					{
						_result = static_cast<longlong_t>(bytes);
						return true;
					}
					break;
				}

				const ssize_t bytes = ::recv(fd, _request.buffer, _request.length, 0);
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::SEND:
			{
				if (_request.chain != nullptr)
				{
					auto iov = _request.chain->AsIovec();

					msghdr message{};
					message.msg_iov = iov.data();
					message.msg_iovlen = iov.size();

					const ssize_t bytes = ::sendmsg(fd, &message, 0);
					if (bytes >= 0)
					{
						_result = static_cast<longlong_t>(bytes);
						return true;
					}
					break;
				}

				const ssize_t bytes = ::send(fd, _request.buffer, _request.length, 0);
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::RECEIVE_FROM:
			{
				socklen_t fromLength = _request.fromAddressLength ? static_cast<socklen_t>(*_request.fromAddressLength) : 0;
				const ssize_t bytes = ::recvfrom(fd, _request.buffer, _request.length, 0, static_cast<sockaddr*>(_request.fromAddress), &fromLength);
				if (bytes >= 0)
				{
					if (_request.fromAddressLength) *_request.fromAddressLength = static_cast<int_t>(fromLength);
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::SEND_TO:
			{
				const ssize_t bytes = ::sendto(fd, _request.buffer, _request.length, 0, static_cast<const sockaddr*>(_request.address), static_cast<socklen_t>(_request.addressLength));
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::SEND_ZERO_COPY:
			{
				// MSG_ZEROCOPY 는 커널이 유저 페이지를 직접 참조해 복사를 생략하는 리눅스 전용 플래그.
				const ssize_t bytes = ::send(fd, _request.buffer, _request.length, MSG_ZEROCOPY);
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::SEND_FILE:
			{
				// sendfile 은 파일 데이터를 유저 공간으로 복사하지 않고 커널 내부에서 소켓으로 바로 전송한다(zero-copy).
				off_t offset = static_cast<off_t>(_request.offset);

				const ssize_t bytes = ::sendfile(fd, static_cast<int_t>(_request.auxHandle), &offset, _request.length);
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			default:
				_result = -static_cast<longlong_t>(EOPNOTSUPP);
				return true;
		}

		// EAGAIN/EWOULDBLOCK 은 진짜 실패가 아니라 "아직 준비 안 됨" 이므로 epoll 대기로 넘긴다.
		if (errno == EAGAIN || errno == EWOULDBLOCK) return false;

		_result = -static_cast<longlong_t>(errno);

		return true;
	}

	bool_t EpollEngine::IsWriteDirection(const RequestKind _requestKind) noexcept
	{
		return _requestKind == RequestKind::WRITE || _requestKind == RequestKind::SEND || _requestKind == RequestKind::CONNECT || _requestKind == RequestKind::WRITE_FIXED || _requestKind == RequestKind::SEND_ZERO_COPY || _requestKind == RequestKind::SEND_FILE || _requestKind == RequestKind::SEND_TO ||
				_requestKind == RequestKind::WAIT_WRITABLE;
	}

	void_t EpollEngine::UpdateEpoll(const int_t _fd) noexcept
	{
		// waiter 맵 상태만이 진실의 원천(source of truth)이며, 이 함수는 그 상태를 epoll 커널 자료구조에 동기화한다.
		uint32_t events = 0;
		if (readWaiter.contains(_fd)) events |= EPOLLIN;
		if (writeWaiter.contains(_fd)) events |= EPOLLOUT;

		if (events == 0)
		{
			(void_t)::epoll_ctl(epollFd, EPOLL_CTL_DEL, _fd, nullptr);
			return;
		}

		epoll_event event{};
		event.events = events;
		event.data.fd = _fd;

		// 이미 등록된 fd 는 MOD 로 갱신하고, 아직 등록 안 된 fd 는 MOD 가 ENOENT 로 실패하므로 ADD 로 폴백한다.
		if (::epoll_ctl(epollFd, EPOLL_CTL_MOD, _fd, &event) != 0) (void_t)::epoll_ctl(epollFd, EPOLL_CTL_ADD, _fd, &event);
	}

	void_t EpollEngine::ProcessCancels()
	{
		// mutex 를 오래 잡지 않도록 pendingCancels 를 통째로 스왑해 락 밖에서 순회한다.
		std::vector<void_t*> cancels;
		{
			std::lock_guard lock(mutex);
			cancels.swap(pendingCancels);
		}

		for (void_t* userData : cancels)
		{
			std::lock_guard lock(mutex);
			const auto iterator = pending.find(userData);
			if (iterator == pending.end()) continue; // 이미 완료되었거나 존재하지 않는 요청은 무시.

			const int_t fd = static_cast<int_t>(iterator->second.request.handle);
			if (iterator->second.isWrite) writeWaiter.erase(fd);
			else readWaiter.erase(fd);
			pending.erase(iterator);
			UpdateEpoll(fd);

			// 취소를 합성 완료(ECANCELED)로 변환해 다음 WaitCompletions() 에서 사용자에게 통지되게 한다.
			ready.push_back(Completion{ userData, -static_cast<longlong_t>(ECANCELED) });
		}
	}
END_NS

#endif // IS_POSIX
