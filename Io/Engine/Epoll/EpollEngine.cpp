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
#		define MSG_ZEROCOPY 0x4000000 // 커널 4.14+ (linux/socket.h) — 오래된 헤더 대비 폴백 정의
#	endif



BEGIN_NS(ne::io)
	EpollEngine::EpollEngine() noexcept
	{
		epollFd = ::epoll_create1(0);
		if (epollFd < 0) return;

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
		if (longlong_t result = 0; Perform(_request, false, result))
		{
			std::lock_guard lock(mutex);
			ready.push_back(Completion{ _request.userData, result });
			return;
		}

		// EAGAIN/EINPROGRESS — epoll 에 등록하고 준비되면 재수행.
		const int_t fd = static_cast<int_t>(_request.handle);
		const bool_t isWrite = IsWriteDirection(_request.op);

		std::lock_guard lock(mutex);

		pending[_request.userData] = PendingOperation{ _request, isWrite };
		if (isWrite) writeWaiter[fd] = _request.userData;
		else readWaiter[fd] = _request.userData;

		UpdateEpoll(fd);
	}
	int_t EpollEngine::WaitCompletions(Completion* _out, const int_t _max, const std::chrono::milliseconds _timeout)
	{
		if (_max <= 0) return 0;

		ProcessCancels();

		// 합성 완료(즉시 완료/취소) 우선 배출.
		{
			std::lock_guard lock(mutex);

			int_t drained = 0;
			while (drained < _max && !ready.empty())
			{
				_out[drained++] = ready.back();
				ready.pop_back();
			}

			if (drained > 0) return drained;
		}

		epoll_event events[MaxEvents];
		const int_t timeoutMs = _timeout.count() < 0 ? -1 : static_cast<int_t>(_timeout.count());
		const int_t eventCount = ::epoll_wait(epollFd, events, MaxEvents, timeoutMs);
		if (eventCount <= 0) return 0; // 타임아웃/인터럽트

		int_t count = 0;
		for (int_t i = 0; i < eventCount && count < _max; ++i)
		{
			const int_t fd = events[i].data.fd;

			if (fd == wakeEventFd)
			{
				uint64_t drained = 0;
				(void_t)::read(wakeEventFd, &drained, sizeof(drained));
				continue;
			}

			// 이 fd 에서 준비된 방향의 대기 op 를 재수행한다(양방향 모두 검사).
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
						waiter.erase(iterator);
						continue;
					}

					operation = pendingIterator->second;
					isFound = true;
				}
				if (!isFound) continue;

				longlong_t result = 0;
				if (!Perform(operation.request, true, result)) continue; // 아직 EAGAIN — 계속 대기

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
			case Capability::SendFileZeroCopy:
				return true; // sendfile(2)
			case Capability::SendMemZeroCopy:
				return true; // MSG_ZEROCOPY
			case Capability::RecvOverheadReduced:
				return false; // 등록 버퍼 없음(plain epoll, io_uring 아님)
			case Capability::RecvTrueZeroCopy:
				return false; // TCP_ZEROCOPY_RECEIVE 는 후속
		}

		return false;
	}
	bool_t EpollEngine::IsWriteDirection(const OpCode _op) noexcept
	{
		return _op == OpCode::Write || _op == OpCode::Send || _op == OpCode::Connect || _op == OpCode::WriteFixed || _op == OpCode::SendZeroCopy || _op == OpCode::SendFile || _op == OpCode::SendTo ||
				_op == OpCode::WaitWritable;
	}
	bool_t EpollEngine::Perform(const Request& _request, const bool_t _isRetry, longlong_t& _result) noexcept
	{
		const int_t fd = static_cast<int_t>(_request.handle);

		switch (_request.op)
		{
			case OpCode::Read:
			{
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
			case OpCode::Write:
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
			case OpCode::Receive:
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
			case OpCode::Send:
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
			case OpCode::SendTo: // 비연결형(UDP) 송신 — address/addressLength 가 매 호출 목적지
			{
				const ssize_t bytes = ::sendto(fd, _request.buffer, _request.length, 0, static_cast<const sockaddr*>(_request.address), static_cast<socklen_t>(_request.addressLength));
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case OpCode::ReceiveFrom: // 비연결형(UDP) 수신 — fromAddress/fromAddressLength 에 발신자 주소를 채움
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
			case OpCode::ReadFixed: // 등록 버퍼 지원 없음(plain epoll) — 일반 read 로 폴백, bufferId 무시
			{
				const ssize_t bytes = ::pread(fd, _request.buffer, _request.length, static_cast<off_t>(_request.offset));
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case OpCode::WriteFixed: // 위와 동일
			{
				const ssize_t bytes = ::pwrite(fd, _request.buffer, _request.length, static_cast<off_t>(_request.offset));
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case OpCode::SendZeroCopy: // MSG_ZEROCOPY — 버퍼 등록 불필요(opportunistic). 재사용 안전성(완료 통지)은
			{                          // 이 구현에서 추적하지 않음 — 호출자가 데이터가 실제 전송될 때까지 버퍼를 살려둘 것.
				const ssize_t bytes = ::send(fd, _request.buffer, _request.length, MSG_ZEROCOPY);
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case OpCode::SendFile: // handle=목적지 소켓(Send 계열과 동일), auxHandle=원본 파일
			{
				off_t offset = static_cast<off_t>(_request.offset);

				const ssize_t bytes = ::sendfile(fd, static_cast<int_t>(_request.auxHandle), &offset, _request.length);
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case OpCode::WaitReadable:
			case OpCode::WaitWritable:
				// readiness 대기 — 실제 read/write 없이 EPOLLIN/EPOLLOUT(IsWriteDirection 이 방향 결정)만 기다린다.
				// 첫 호출은 미준비로 등록(false), epoll 이 준비를 알린 재수행에서 ready(result 0)로 완료.
				if (!_isRetry) return false;
				_result = 0;
				return true;

			case OpCode::Accept:
			{
				const int_t accepted = ::accept(fd, nullptr, nullptr);
				if (accepted >= 0)
				{
					_result = static_cast<longlong_t>(accepted);
					return true;
				}
				break;
			}
			case OpCode::Connect:
			{
				if (_isRetry)
				{
					// EPOLLOUT 준비 후 재진입 — SO_ERROR 로 연결 성공/실패 확정.
					int_t soError = 0;
					socklen_t length = sizeof(soError);
					(void_t)::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &length);
					_result = (soError == 0) ? 0 : -static_cast<longlong_t>(soError);
					return true;
				}

				if (::connect(fd, static_cast<const sockaddr*>(_request.address), static_cast<socklen_t>(_request.addressLength)) == 0)
				{
					_result = 0;
					return true; // 즉시 연결(로컬 등)
				}

				if (errno == EINPROGRESS) return false; // EPOLLOUT 대기
				break;
			}
			default:
				_result = -static_cast<longlong_t>(EOPNOTSUPP); // Level 3.5 op 은 후속 Phase
				return true;
		}

		if (errno == EAGAIN || errno == EWOULDBLOCK) return false; // epoll 대기 필요

		_result = -static_cast<longlong_t>(errno);

		return true;
	}
	void_t EpollEngine::UpdateEpoll(const int_t _fd) noexcept
	{
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

		if (::epoll_ctl(epollFd, EPOLL_CTL_MOD, _fd, &event) != 0) (void_t)::epoll_ctl(epollFd, EPOLL_CTL_ADD, _fd, &event); // 미등록이면 ADD
	}
	void_t EpollEngine::ProcessCancels()
	{
		std::vector<void_t*> cancels;
		{
			std::lock_guard lock(mutex);
			cancels.swap(pendingCancels);
		}

		for (void_t* userData : cancels)
		{
			std::lock_guard lock(mutex);
			const auto iterator = pending.find(userData);
			if (iterator == pending.end()) continue;

			const int_t fd = static_cast<int_t>(iterator->second.request.handle);
			if (iterator->second.isWrite) writeWaiter.erase(fd);
			else readWaiter.erase(fd);
			pending.erase(iterator);
			UpdateEpoll(fd);

			ready.push_back(Completion{ userData, -static_cast<longlong_t>(ECANCELED) });
		}
	}
END_NS

#endif // IS_POSIX
