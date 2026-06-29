//
// Created by nebula on 24. 5. 29.
//

#include "TcpSocket.h"

#include <thread>
#if defined(IS_POSIX)
#	include <fcntl.h>
#	if defined(__linux__)
#		include <sys/epoll.h>
#	elif defined(__APPLE__)
#		include <sys/event.h>
#		include <sys/types.h>
#	endif
#endif
#include "StringFormat.h"



BEGIN_NS(ne::protocol)
	TcpSocket::TcpSocket(const string_view_t _server, const int_t _port)
		: addressInfo(GetAddressInfo(_server, _port))
		, handle(CreateHandle())
	{
	}



	void_t TcpSocket::Connect()
	{
		using namespace std::chrono_literals;

		if (timeout.count() <= 0)
		{
			while (connect(handle.Get(), addressInfo->ai_addr, static_cast<int_t>(addressInfo->ai_addrlen)) == -1)
			{
#if defined(_WIN32)
				constexpr int_t Inprogress = WSAEINPROGRESS;
#elif defined(IS_POSIX)
				constexpr int_t Inprogress = EINPROGRESS;
#endif
				if (const auto error = GetSocketError(); error != Inprogress)
				{
					throw ne::Exception("[TcpSocket/Connect]", std::format("Failed to connect socket (error: {})", error));
				}
				std::this_thread::sleep_for(1ms);
			}

			return;
		}

		SetSocketMode(true);

		if (connect(handle.Get(), addressInfo->ai_addr, static_cast<int_t>(addressInfo->ai_addrlen)) == -1)
		{
#if defined(_WIN32)
			constexpr int_t WouldBlock = WSAEWOULDBLOCK;
#elif defined(IS_POSIX)
			constexpr int_t WouldBlock = EINPROGRESS;
#endif
			if (const auto error = GetSocketError(); error != WouldBlock)
			{
				SetSocketMode(false);
				throw ne::Exception("[TcpSocket/Connect]", std::format("Failed to connect socket (error: {})", error));
			}

			auto pollFd = pollfd{ .fd = handle.Get(), .events = POLLOUT };
#if defined(_WIN32)
			const auto pollResult = ::WSAPoll(&pollFd, 1, static_cast<int_t>(timeout.count()));
#elif defined(IS_POSIX)
			const auto pollResult = ::poll(&pollFd, 1, static_cast<int_t>(timeout.count()));
#endif
			if (pollResult == 0)
			{
				SetSocketMode(false);
				throw ne::Exception("[TcpSocket/Connect]", "Connection attempt timed out");
			}
			if (pollResult < 0)
			{
				const auto error = GetSocketError();
				SetSocketMode(false);
				throw ne::Exception("[TcpSocket/Connect]", std::format("Failed to poll socket while connecting (error: {})", error));
			}

			int_t socketError = 0;
			int_t socketErrorLength = sizeof(socketError);
			::getsockopt(handle.Get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorLength);
			if (socketError != 0)
			{
				SetSocketMode(false);
				throw ne::Exception("[TcpSocket/Connect]", std::format("Failed to connect socket (error: {})", socketError));
			}
		}

		SetSocketMode(false);
	}

	void_t TcpSocket::Reconnect()
	{
		handle = CreateHandle();
		Connect();
	}

	bool_t TcpSocket::IsConnected() const
	{
#if defined(_WIN32)
		return handle.Get() != INVALID_SOCKET;
#elif defined(IS_POSIX)
		return handle.Get() != -1;
#endif
	}

	bool_t TcpSocket::IsAlive()
	{
		if (!IsConnected()) return false;

		const auto wasNonblocking = isNonblocking;
		SetSocketMode(true);

		char probe;
		const auto result = ::recv(handle.Get(), &probe, 1, MSG_PEEK);

		SetSocketMode(wasNonblocking);

		if (result == 0) return false;
		if (result > 0) return true;

		const auto error = GetSocketError();
#if defined(_WIN32)
		return error == WSAEWOULDBLOCK;
#elif defined(IS_POSIX)
		return error == EWOULDBLOCK || error == EAGAIN;
#endif
	}

	void_t TcpSocket::Bind()
	{
		if (::bind(handle.Get(), addressInfo->ai_addr, static_cast<socklen_t>(addressInfo->ai_addrlen)) == -1)
		{
			throw ne::Exception("[TcpSocket/Bind]", std::format("Failed to bind socket (result: {})", GetSocketError()));
		}
	}

	void_t TcpSocket::Listen()
	{
		if (::listen(handle.Get(), SOMAXCONN) == -1)
		{
			throw ne::Exception("[TcpSocket/Listen]", std::format("Failed to listen socket (result: {})", GetSocketError()));
		}
	}

	socket_t TcpSocket::Accept(sockaddr* _sockAddr, socklen_t* _addrLength)
	{
		auto socket = ::accept(handle.Get(), _sockAddr, _addrLength);
#if defined(_WIN32)
		if (socket == INVALID_SOCKET)
		{
			throw ne::Exception("[TcpSocket/Accept]", std::format("Failed to accept socket (result: {})", GetSocketError()));
		}
#elif defined(IS_POSIX)
		if (socket == -1)
		{
			throw ne::Exception("[TcpSocket/Accept]", std::format("Failed to accept socket (result: {})", GetSocketError()));
		}
#endif

		return socket;
	}


	void_t TcpSocket::Select()
	{
		fd_set readFds, writeFds, exceptFds;
		FD_ZERO(&readFds);
		FD_ZERO(&writeFds);
		FD_ZERO(&exceptFds);

		FD_SET(handle.Get(), &readFds);
		FD_SET(handle.Get(), &writeFds);
		FD_SET(handle.Get(), &exceptFds);

		if (::select(0, &readFds, &writeFds, &exceptFds, nullptr) < 0)
		{
			throw ne::Exception("[TcpSocket/Select]", std::format("Failed to select socket (result: {})", GetSocketError()));
		}

		if (FD_ISSET(handle.Get(), &readFds)) ProcessReadEvent();
		if (FD_ISSET(handle.Get(), &writeFds)) ProcessWriteEvent();
		if (FD_ISSET(handle.Get(), &exceptFds)) ProcessErrorEvent();
	}

	void_t TcpSocket::Poll()
	{
		pollfd pollFds[1];
		pollFds[0].fd = handle.Get();
		pollFds[0].events = POLLIN | POLLOUT | POLLERR;

#if defined(_WIN32)
		if (::WSAPoll(pollFds, 1, 0) < 0)
		{
			throw ne::Exception("[TcpSocket/Poll]", std::format("Failed to poll socket (result: {})", GetSocketError()));
		}
#elif defined(IS_POSIX)
		if (::poll(pollFds, 1, 0) < 0)
		{
			throw ne::Exception("[TcpSocket/Poll]", std::format("Failed to poll socket (result: {})", GetSocketError()));
		}
#endif

		if (pollFds[0].revents & POLLIN) ProcessReadEvent();
		if (pollFds[0].revents & POLLOUT) ProcessWriteEvent();
		if (pollFds[0].revents & POLLERR) ProcessErrorEvent();
	}
#if defined(_WIN32)
	void_t TcpSocket::Iocp()
	{
		// Create a dedicated IOCP with one concurrent thread (this thread is the sole poller).
		HANDLE iocpFd = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
		if (!iocpFd)
			throw ne::Exception("[TcpSocket/Iocp]", std::format("Failed to create IOCP (error: {})", ::GetLastError()));

		// Associate the socket with the IOCP. From here all overlapped ops on this
		// socket will post completion packets to iocpFd.
		if (!::CreateIoCompletionPort(reinterpret_cast<HANDLE>(handle.Get()), iocpFd, 0, 0))
		{
			::CloseHandle(iocpFd);
			throw ne::Exception("[TcpSocket/Iocp]", std::format("Failed to associate socket with IOCP (error: {})", ::GetLastError()));
		}

		// Per-operation context.  OVERLAPPED must be the first member so that
		// reinterpret_cast<ReadContext*>(OVERLAPPED*) is always safe.
		struct ReadContext
		{
			OVERLAPPED             overlapped{};
			WSABUF                 wsaBuf{};
			std::array<BYTE, 4096> buffer{};
		};

		auto postRead = [this](ReadContext* _ctx) -> bool_t
		{
			_ctx->overlapped = {};
			_ctx->wsaBuf.buf = reinterpret_cast<CHAR*>(_ctx->buffer.data());
			_ctx->wsaBuf.len = static_cast<ULONG>(_ctx->buffer.size());
			DWORD flags      = 0;
			if (::WSARecv(handle.Get(), &_ctx->wsaBuf, 1, nullptr, &flags, &_ctx->overlapped, nullptr) == SOCKET_ERROR)
				return ::WSAGetLastError() == WSA_IO_PENDING;
			return true;
		};

		// Issue the first read; ownership of the context transfers to the IOCP.
		auto* pending = new ReadContext{};
		if (!postRead(pending))
		{
			delete pending;
			::CloseHandle(iocpFd);
			throw ne::Exception("[TcpSocket/Iocp]", std::format("Failed to post initial WSARecv (error: {})", ::WSAGetLastError()));
		}

		while (IsConnected())
		{
			DWORD       bytes = 0;
			ULONG_PTR   key   = 0;
			OVERLAPPED* pOv   = nullptr;

			const auto ok = ::GetQueuedCompletionStatus(iocpFd, &bytes, &key, &pOv, INFINITE);

			// Take back ownership; ctx is freed on every exit path below.
			auto ctx = std::unique_ptr<ReadContext>(reinterpret_cast<ReadContext*>(pOv));
			pending  = nullptr;

			if (!ok)
			{
				if (pOv) ProcessErrorEvent();
				break;
			}
			if (bytes == 0) // peer closed the connection gracefully
			{
				handle = SocketHandle{};
				break;
			}

			if (readHandler)
			{
				auto data = std::vector<std::byte>(bytes);
				std::memcpy(data.data(), ctx->buffer.data(), bytes);
				readHandler(std::move(data));
			}

			// Re-post a read using the same context; ownership transfers back to the IOCP.
			if (!postRead(ctx.get()))
			{
				ProcessErrorEvent();
				break;
			}
			pending = ctx.release();
		}

		// If a WSARecv is still in flight, cancel it and drain its completion
		// packet so the ReadContext is freed cleanly before we close the IOCP.
		if (pending)
		{
			::CancelIoEx(reinterpret_cast<HANDLE>(handle.Get()), &pending->overlapped);
			DWORD b; ULONG_PTR k; OVERLAPPED* ov = nullptr;
			::GetQueuedCompletionStatus(iocpFd, &b, &k, &ov, 500);
			if (ov) delete reinterpret_cast<ReadContext*>(ov);
			else    delete pending;
		}

		handle = SocketHandle{};
		::CloseHandle(iocpFd);
	}
#elif defined(__linux__)
	void_t TcpSocket::Epoll()
	{
		const int_t epollFd = ::epoll_create1(EPOLL_CLOEXEC);
		if (epollFd == -1)
			throw ne::Exception("[TcpSocket/Epoll]", std::format("Failed to create epoll fd (error: {})", GetSocketError()));

		epoll_event ev{};
		ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
		ev.data.fd = handle.Get();
		if (::epoll_ctl(epollFd, EPOLL_CTL_ADD, handle.Get(), &ev) == -1)
		{
			::close(epollFd);
			throw ne::Exception("[TcpSocket/Epoll]", std::format("Failed to register socket with epoll (error: {})", GetSocketError()));
		}

		std::array<epoll_event, 16> events{};
		while (IsConnected())
		{
			const int_t count = ::epoll_wait(epollFd, events.data(), static_cast<int_t>(events.size()), -1);
			if (count < 0)
			{
				if (errno == EINTR) continue;
				break;
			}

			for (int_t i = 0; i < count; ++i)
			{
				if (events[i].events & (EPOLLERR | EPOLLHUP)) { ProcessErrorEvent(); break; }
				if (events[i].events & EPOLLIN)  ProcessReadEvent();
				if (events[i].events & EPOLLOUT) ProcessWriteEvent();
			}
		}

		::close(epollFd);
	}
#elif defined(__APPLE__)
	void_t TcpSocket::Kqueue()
	{
		const int_t kqFd = ::kqueue();
		if (kqFd == -1)
			throw ne::Exception("[TcpSocket/Kqueue]", std::format("Failed to create kqueue fd (error: {})", GetSocketError()));

		struct kevent changes[2];
		EV_SET(&changes[0], handle.Get(), EVFILT_READ,  EV_ADD | EV_ENABLE, 0, 0, nullptr);
		EV_SET(&changes[1], handle.Get(), EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);

		if (::kevent(kqFd, changes, 2, nullptr, 0, nullptr) == -1)
		{
			::close(kqFd);
			throw ne::Exception("[TcpSocket/Kqueue]", std::format("Failed to register socket with kqueue (error: {})", GetSocketError()));
		}

		std::array<struct kevent, 16> events{};
		while (IsConnected())
		{
			const int_t count = ::kevent(kqFd, nullptr, 0, events.data(), static_cast<int_t>(events.size()), nullptr);
			if (count < 0)
			{
				if (errno == EINTR) continue;
				break;
			}

			for (int_t i = 0; i < count; ++i)
			{
				if (events[i].flags & EV_ERROR)  { ProcessErrorEvent(); break; }
				if (events[i].filter == EVFILT_READ)  ProcessReadEvent();
				if (events[i].filter == EVFILT_WRITE) ProcessWriteEvent();
			}
		}

		::close(kqFd);
	}
#endif



	void_t TcpSocket::SetSocketOption(int _option, const char* _value, int _valueLength) const
	{
		if (::setsockopt(handle.Get(), SOL_SOCKET, _option, _value, _valueLength) == -1)
		{
			throw ne::Exception("[TcpSocket/SetSocketOption]", std::format("Failed to setsockopt function | {} (error: {})", _option, GetSocketError()));
		}
	}

	void_t TcpSocket::SocketControl(long_t _option, ulong_t _value, bool _isEnable) const
	{
#if defined(_WIN32)
		if (::ioctlsocket(handle.Get(), _option, &_value) == -1)
		{
			throw ne::Exception("[TcpSocket/SetSocketControl]", std::format("Failed to ioctlsocket function | {} (error: {})", _option, GetSocketError()));
		}
#elif defined(IS_POSIX)
		const auto flags = ::fcntl(handle.Get(), F_GETFL);
		if (_isEnable)
		{
			if (::fcntl(handle.Get(), _option, flags | _value) == -1)
			{
				throw ne::Exception("[TcpSocket/SetSocketControl]", std::format("Failed to fcntl function | {} (error: {})", _option, GetSocketError()));
			}
		}
		else
		{
			if (::fcntl(handle.Get(), _option, flags & ~_value) == -1)
			{
				throw ne::Exception("[TcpSocket/SetSocketControl]", std::format("Failed to fcntl function | {} (error: {})", _option, GetSocketError()));
			}
		}
#endif
	}


	void_t TcpSocket::SetSocketMode(bool _isNonblocking)
	{
		if (isNonblocking == _isNonblocking) return;
#if defined(_WIN32)
		SocketControl(FIONBIO, _isNonblocking ? 1 : 0);
#elif defined(IS_POSIX)
		SocketControl(F_SETFL, O_NONBLOCK, _isNonblocking);
#endif
		isNonblocking = _isNonblocking;
	}


	void_t TcpSocket::SetTimeout(const std::chrono::milliseconds _timeout)
	{
		timeout = _timeout;

		if (_timeout.count() <= 0) return;

#if defined(_WIN32)
		const auto millis = static_cast<DWORD>(_timeout.count());
		SetSocketOption(SO_RCVTIMEO, reinterpret_cast<const char*>(&millis), sizeof(millis));
		SetSocketOption(SO_SNDTIMEO, reinterpret_cast<const char*>(&millis), sizeof(millis));
#elif defined(IS_POSIX)
		const auto value = timeval
		{
			.tv_sec = static_cast<long_t>(_timeout.count() / 1000),
			.tv_usec = static_cast<long_t>((_timeout.count() % 1000) * 1000)
		};
		SetSocketOption(SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value));
		SetSocketOption(SO_SNDTIMEO, reinterpret_cast<const char*>(&value), sizeof(value));
#endif
	}



	longlong_t TcpSocket::Read(const std::span<std::byte> _buffer)
	{
		if (!IsConnected()) return -1;

		if (const auto result = recv(handle.Get(), reinterpret_cast<char_t*>(_buffer.data()), static_cast<int_t>(_buffer.size()), 0); result > 0)
		{
			return static_cast<longlong_t>(result);
		}
		else if (result == 0)
		{
			handle = SocketHandle{};
			return -1;
		}

		auto errorCode = GetSocketError();
#if defined(_WIN32)
		if (isNonblocking && errorCode == WSAEWOULDBLOCK) return 0;
#elif defined(IS_POSIX)
		if (isNonblocking && (errorCode == EWOULDBLOCK || errorCode == EAGAIN)) return 0;
#endif

		throw ne::Exception("[TcpSocket/Read]", std::format("Failed to receive data through socket (error: {})", errorCode));
	}

	bool_t TcpSocket::Write(const std::span<const std::byte> _data)
	{
		if (!IsConnected()) return false;

		if (::send(handle.Get(), reinterpret_cast<const char_t*>(_data.data()), static_cast<int_t>(_data.size()), 0) == -1)
		{
			throw ne::Exception("[TcpSocket/Write]", std::format("Failed to send data through socket (error: {})", GetSocketError()));
		}

		return true;
	}



	SocketHandle TcpSocket::CreateHandle() const
	{
		using namespace std::chrono_literals;

		auto handle = SocketHandle{};
		while (!(handle = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol)))
		{
#if defined(_WIN32)
			constexpr int_t Inprogress = WSAEINPROGRESS;
#elif defined(IS_POSIX)
			constexpr int_t Inprogress = EINPROGRESS;
#endif
			if (const auto error = GetSocketError(); error != Inprogress)
			{
				throw ne::Exception("[TcpSocket/CreateHandle]", std::format("Failed to create socket (error: {})", error));
			}
			std::this_thread::sleep_for(100ms);
		}

		return handle;
	}

	TcpSocket::AddressInfo TcpSocket::GetAddressInfo(const string_view_t _server, const int_t _port)
	{
#if defined(_WIN32)
		const auto server = StringFormat::UTF8toWCS(string_t(_server).c_str());
		const auto port = std::to_wstring(_port);
		constexpr auto hints = addrinfoW
		{
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
			.ai_protocol = IPPROTO_TCP,
		};
		auto addressInfo = static_cast<addrinfoW*>(nullptr);

		if (const auto result = GetAddrInfoW(server.data(), port.data(), &hints, &addressInfo))
		{
			throw ne::Exception("[TcpSocket/GetAddressInfo]", std::format("Failed to get address info for socket creation (result: {})", result));
		}
#elif defined(IS_POSIX)
		const auto port = std::to_string(_port);
		constexpr auto hints = addrinfo{
			.ai_flags{},
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
			.ai_protocol = IPPROTO_TCP,
			.ai_addrlen{},
			.ai_addr{},
			.ai_canonname{},
			.ai_next{}
		};
		auto addressInfo = static_cast<addrinfo*>(nullptr);

		if (const auto result = ::getaddrinfo(_server.data(), port.data(), &hints, &addressInfo))
		{
			throw ne::Exception("[TcpSocket/GetAddressInfo]", std::format("Failed to get address info for socket creation (result: {})", result));
		}
#endif

		return AddressInfo(addressInfo);
	}



	void_t TcpSocket::ProcessReadEvent()
	{
		try
		{
			auto data = std::vector<std::byte>(4096);
			if (const auto readSize = Read(data); readSize > 0)
			{
				data.resize(readSize);
				if (readHandler) readHandler(data);
			}
		} catch (const ne::Exception& e)
		{
			if (exceptionHandler) exceptionHandler(e.what());
		}
	}

	void_t TcpSocket::ProcessWriteEvent()
	{
		if (writeQueue.empty()) return;

		try
		{
			Write(writeQueue.front());
			writeQueue.pop();
		} catch (const ne::Exception& e)
		{
			if (exceptionHandler) exceptionHandler(e.what());
		}
	}

	void_t TcpSocket::ProcessErrorEvent()
	{
		int_t error = 0;
		int_t errorLength = sizeof(error);
		if (getsockopt(handle.Get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &errorLength) != 0)
		{
			if (exceptionHandler) exceptionHandler(std::format("[TcpSocket/ProcessErrorEvent] Exception socket (result: {})", error));
		}
		else
		{
			if (exceptionHandler) exceptionHandler(std::format("[TcpSocket/ProcessErrorEvent] Failed to getsockopt funciton (result: {})", GetSocketError()));
		}
	}



	socket_t TcpSocket::ReleaseHandle() noexcept
	{
		const auto rawHandle = handle.Get();
		handle.Get() = SocketHandle{}.Get();

		return rawHandle;
	}

END_NS
