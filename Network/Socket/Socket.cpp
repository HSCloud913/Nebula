//
// Created by hscloud on 25. 6. 29.
//

#include "Socket.h"

#include <cstring>
#include <cerrno>
#include <coroutine>
#include <optional>
#include <type_traits>
#include <utility>
#include "ThreadPool.h"
#include "Coroutine/Awaitable.h"
#if defined(_WIN32)
#   include <winsock2.h>
#   include <ws2tcpip.h>
#elif defined(IS_POSIX)
#   include <netinet/tcp.h>
#endif



BEGIN_NS(ne::network::dns)
	// DNS 조회(getaddrinfo) 전용 워커 풀. 호출 스레드(이벤트 루프)를 막지 않기 위해
	// 블로킹 작업을 여기로 넘기고, 완료되면 작업 스레드에서 코루틴을 재개한다.
	ne::ThreadPool& WorkerPool()
	{
		static ne::ThreadPool pool(2);
		return pool;
	}

	template <typename F>
	class ResolveAwaitable
	{
	public:
		explicit ResolveAwaitable(F _function) noexcept
			: function(std::move(_function)) {}

		[[nodiscard]] ne::bool_t await_ready() const noexcept { return false; }

		void await_suspend(const std::coroutine_handle<> _handle)
		{
			WorkerPool().Enqueue([this, _handle]() mutable
			{
				result.emplace(function());
				_handle.resume();
			});
		}

		[[nodiscard]] auto await_resume() noexcept { return std::move(*result); }

	private:
		F function;
		std::optional<std::invoke_result_t<F>> result;
	};

END_NS



/*--------------------------------------------------*/



BEGIN_NS(ne::network)
	Socket::Socket(const socket_t _fd, const AddressFamily _family, const int_t _type, const int_t _protocol)
		: handle(_fd)
		, family(_family)
		, type(_type)
		, protocol(_protocol) {}



	Result<Socket, OsError> Socket::Create(const AddressFamily _family, const int_t _type, const int_t _protocol,
		const SocketCreateFlags _flags)
	{
		const int_t af = (_family == AddressFamily::IPv6) ? AF_INET6 : AF_INET;

	#if defined(_WIN32)
		// RIO 소켓은 WSASocketW + WSA_FLAG_REGISTERED_IO 로 만들어야 RIORegisterBuffer/
		// RIOCreateRequestQueue 가 성립한다. WSA_FLAG_OVERLAPPED 도 함께 줘 IOCP 완료 통지를 쓴다.
		const socket_t fd = HasFlag(_flags, SocketCreateFlags::RegisteredIo)
			? ::WSASocketW(af, _type, _protocol, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO)
			: ::socket(af, _type, _protocol);
	#elif defined(IS_POSIX)
		(void)_flags; // RegisteredIo 는 POSIX 소켓 생성에 영향 없음(io_uring 등록버퍼는 소켓 플래그 불요).
		const socket_t fd = ::socket(af, _type, _protocol);
	#endif

		if (fd == InvalidSocket)
			return ne::Result<Socket, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/Create]")
			);

		return ne::Result<Socket, ne::OsError>::Ok(Socket{ fd, _family, _type, _protocol });
	}


	ne::Result<AddressFamily, ne::OsError> Socket::ResolveFamilyBlocking(const string_view_t _address)
	{
		const ne::string_t addressString(_address);

		in_addr v4{};
		if (::inet_pton(AF_INET, addressString.c_str(), &v4) == 1) return ne::Result<AddressFamily, ne::OsError>::Ok(AddressFamily::IPv4);

		in6_addr v6{};
		if (::inet_pton(AF_INET6, addressString.c_str(), &v6) == 1) return ne::Result<AddressFamily, ne::OsError>::Ok(AddressFamily::IPv6);

		// 호스트 이름 — DNS 에게 우선순위를 맡기고 첫 결과의 패밀리를 취한다.
		addrinfo hints{};
		addrinfo* result = nullptr;
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		if (::getaddrinfo(addressString.c_str(), nullptr, &hints, &result) != 0)
			return ne::Result<AddressFamily, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/ResolveFamily]")
			);

		const AddressFamily family = result->ai_family == AF_INET6 ? AddressFamily::IPv6 : AddressFamily::IPv4;
		::freeaddrinfo(result);

		return ne::Result<AddressFamily, ne::OsError>::Ok(family);
	}

	ne::Task<ne::Result<AddressFamily, ne::OsError>> Socket::ResolveFamily(const string_view_t _address)
	{
		ne::string_t addressString(_address);
		co_return co_await dns::ResolveAwaitable([addressString = std::move(addressString)] { return ResolveFamilyBlocking(addressString); });
	}



	ne::Result<void, ne::OsError> Socket::SetReuseAddress(const bool_t _enable)
	{
		const int_t value = _enable ? 1 : 0;
		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char_t*>(&value), sizeof(value)) != 0)
		{
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/SetReuseAddress]")
			);
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> Socket::SetNoDelay(const bool_t _enable)
	{
		const int_t value = _enable ? 1 : 0;
		if (::setsockopt(handle.Get(), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char_t*>(&value), sizeof(value)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/SetNoDelay]")
			);

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> Socket::SetNonBlocking(const bool_t _enable)
	{
#if defined(_WIN32)
		ulong_t mode = _enable ? 1u : 0u;
		if (::ioctlsocket(handle.Get(), FIONBIO, &mode) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/SetNonBlocking]")
			);
#elif defined(IS_POSIX)
		int_t flags = ::fcntl(handle.Get(), F_GETFL, 0);
		if (flags == -1)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[Socket/SetNonBlocking]")
			);

		flags = _enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
		if (::fcntl(handle.Get(), F_SETFL, flags) == -1)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[Socket/SetNonBlocking]")
			);
#endif

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> Socket::SetSendTimeout(const std::chrono::milliseconds _timeout)
	{
#if defined(_WIN32)
		const ulong_t ms = _timeout.count();
		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char_t*>(&ms), sizeof(ms)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/SetSendTimeout]")
			);

#elif defined(IS_POSIX)
		const timeval tv{
			.tv_sec = static_cast<time_t>(_timeout.count() / 1000),
			.tv_usec = static_cast<suseconds_t>((_timeout.count() % 1000) * 1000)
		};

		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char_t*>(&tv), sizeof(tv)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[Socket/SetSendTimeout]")
			);

#endif

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> Socket::SetReceiveTimeout(const std::chrono::milliseconds _timeout)
	{
	#if defined(_WIN32)
		const ulong_t ms = _timeout.count();
		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char_t*>(&ms), sizeof(ms)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/SetReceiveTimeout]")
			);

	#elif defined(IS_POSIX)
		const timeval tv{
			.tv_sec = static_cast<time_t>(_timeout.count() / 1000),
			.tv_usec = static_cast<suseconds_t>((_timeout.count() % 1000) * 1000)
		};

		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char_t*>(&tv), sizeof(tv)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[Socket/SetReceiveTimeout]")
			);

	#endif

		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Task<ne::Result<void, ne::OsError>> Socket::Connect(const string_view_t _address, const uint16_t _port)
	{
		ne::string_t addressString(_address);

		auto resolved = co_await dns::ResolveAwaitable([this, addressString = std::move(addressString), _port] { return ResolveCandidates(addressString, _port); });
		if (!resolved)
		{
			resolved.Error().Context("[Socket/Connect]");
			co_return ne::Result<void, ne::OsError>::Error(std::move(resolved.Error()));
		}

		co_return ConnectResolved(resolved.Value());
	}



	ne::Task<ne::Result<void, ne::OsError>> Socket::Connect(const string_view_t _address, const uint16_t _port, ne::io::IIoEngine& _engine)
	{
		ne::string_t addressString(_address);

		auto resolved = co_await dns::ResolveAwaitable([this, addressString = std::move(addressString), _port] { return ResolveCandidates(addressString, _port); });
		if (!resolved)
		{
			resolved.Error().Context("[Socket/Connect]");
			co_return ne::Result<void, ne::OsError>::Error(std::move(resolved.Error()));
		}

		co_return co_await ConnectResolvedAsync(resolved.Value(), _engine);
	}



	ne::Task<ne::Result<void, ne::OsError>> Socket::Bind(const string_view_t _address, const uint16_t _port)
	{
		ne::string_t addressString(_address);
		auto resolved = co_await dns::ResolveAwaitable([this, addressString = std::move(addressString), _port] { return ResolveAddress(addressString, _port); });
		if (!resolved)
		{
			resolved.Error().Context("[Socket/Bind]");
			co_return ne::Result<void, ne::OsError>::Error(std::move(resolved.Error()));
		}

		if (::bind(handle.Get(), reinterpret_cast<const sockaddr*>(&resolved.Value()), resolved.Value().ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in)) != 0)
			co_return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/Bind]")
			);

		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> Socket::Listen(const int_t _backlog)
	{
		if (::listen(handle.Get(), _backlog) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/Listen]")
			);

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<Socket, ne::OsError> Socket::Accept()
	{
		sockaddr_storage address{};
		socklen_t addressLength = sizeof(address);

		const socket_t clientFd = ::accept(handle.Get(), reinterpret_cast<sockaddr*>(&address), &addressLength);
		if (clientFd == InvalidSocket)
			return ne::Result<Socket, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/Accept]")
			);

		// accept 된 소켓은 리스너와 같은 패밀리/타입/프로토콜을 물려받는다.
		return ne::Result<Socket, ne::OsError>::Ok(Socket{ clientFd, family, type, protocol });
	}



	ne::Result<sockaddr_storage, ne::OsError> Socket::ResolveAddress(const string_view_t _address, const uint16_t _port) const
	{
		auto candidates = ResolveCandidates(_address, _port);
		if (!candidates)
			return ne::Result<sockaddr_storage, ne::OsError>::Error(std::move(candidates.Error()));

		return ne::Result<sockaddr_storage, ne::OsError>::Ok(candidates.Value().front());
	}

	ne::Result<std::vector<sockaddr_storage>, ne::OsError> Socket::ResolveCandidates(const string_view_t _address, const uint16_t _port) const
	{
		const ne::string_t addressString(_address);

		sockaddr_storage literal{};
		if (family == AddressFamily::IPv4)
		{
			auto& literalV4 = reinterpret_cast<sockaddr_in&>(literal);
			if (::inet_pton(AF_INET, addressString.c_str(), &literalV4.sin_addr) == 1)
			{
				literalV4.sin_family = AF_INET;
				literalV4.sin_port = ::htons(_port);

				return ne::Result<std::vector<sockaddr_storage>, ne::OsError>::Ok({ literal });
			}
		}
		else
		{
			auto& literalV6 = reinterpret_cast<sockaddr_in6&>(literal);
			if (::inet_pton(AF_INET6, addressString.c_str(), &literalV6.sin6_addr) == 1)
			{
				literalV6.sin6_family = AF_INET6;
				literalV6.sin6_port = ::htons(_port);

				return ne::Result<std::vector<sockaddr_storage>, ne::OsError>::Ok({ literal });
			}
		}

		// 호스트 이름 → 소켓 자신의 패밀리에 맞는 IP 들로 변환. 여러 레코드(A/AAAA)가 있으면
		// 전부 모아서 Connect 가 순차적으로 페일오버할 수 있게 한다.
		addrinfo hints{};
		addrinfo* result = nullptr;
		hints.ai_family = family == AddressFamily::IPv6 ? AF_INET6 : AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		if (::getaddrinfo(addressString.c_str(), nullptr, &hints, &result) != 0)
			return ne::Result<std::vector<sockaddr_storage>, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/ResolveCandidates]")
			);

		std::vector<sockaddr_storage> candidates;
		for (const addrinfo* candidate = result; candidate != nullptr; candidate = candidate->ai_next)
		{
			sockaddr_storage storage{};
			std::memcpy(&storage, candidate->ai_addr, candidate->ai_addrlen);

			if (family == AddressFamily::IPv4)
				reinterpret_cast<sockaddr_in&>(storage).sin_port = ::htons(_port);
			else
				reinterpret_cast<sockaddr_in6&>(storage).sin6_port = ::htons(_port);

			candidates.push_back(storage);
		}

		::freeaddrinfo(result);

		return ne::Result<std::vector<sockaddr_storage>, ne::OsError>::Ok(std::move(candidates));
	}

	ne::Result<void, ne::OsError> Socket::ConnectResolved(const std::vector<sockaddr_storage>& _candidates)
	{
		ne::OsError lastError{ 0 };

		for (std::size_t i = 0; i < _candidates.size(); ++i)
		{
			// 이전 후보의 connect() 가 실패한 소켓은 재사용하지 않고 새로 연다 —
			// 실패한 소켓으로 connect() 를 재시도했을 때의 동작은 플랫폼마다 다르다.
			if (i > 0)
			{
				const socket_t fd = ::socket(family == AddressFamily::IPv6 ? AF_INET6 : AF_INET, type, protocol);
				if (fd == InvalidSocket)
				{
					lastError = ne::OsError{ LastOsError() };
					continue;
				}

				handle = fd;
			}

			const auto& candidate = _candidates[i];
			if (::connect(handle.Get(), reinterpret_cast<const sockaddr*>(&candidate), candidate.ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in)) == 0)
				return ne::Result<void, ne::OsError>::Ok();

			const ulong_t lastOsError = LastOsError();
	#if defined(_WIN32)
			if (lastOsError == WSAEINPROGRESS || lastOsError == WSAEWOULDBLOCK)
				return ne::Result<void, ne::OsError>::Ok();
	#elif defined(IS_POSIX)
			if (errno == EINPROGRESS)
				return ne::Result<void, ne::OsError>::Ok();
	#endif

			lastError = ne::OsError{ lastOsError };
		}

		return ne::Result<void, ne::OsError>::Error(lastError.Context("[Socket/Connect]"));
	}



	ne::Task<ne::Result<void, ne::OsError>> Socket::ConnectResolvedAsync(const std::vector<sockaddr_storage>& _candidates, ne::io::IIoEngine& _engine)
	{
		ne::OsError lastError{ 0 };

		for (std::size_t i = 0; i < _candidates.size(); ++i)
		{
			// 이전 후보의 connect() 가 실패한 소켓은 재사용하지 않고 새로 연다 —
			// 실패한 소켓으로 connect() 를 재시도했을 때의 동작은 플랫폼마다 다르다.
			if (i > 0)
			{
				const socket_t fd = ::socket(family == AddressFamily::IPv6 ? AF_INET6 : AF_INET, type, protocol);
				if (fd == InvalidSocket)
				{
					lastError = ne::OsError{ LastOsError() };
					continue;
				}

				handle = fd;
			}

			// connect() 전에 non-blocking 으로 전환 — 그렇지 않으면 이 syscall 이 상대가 응답할
			// 때까지(OS 기본 타임아웃, 수십 초) 이 코루틴을 재개한 스레드(보통 DNS 워커 스레드)를
			// 그대로 점유한다. 실패해도 이 후보는 어차피 버려지므로 별도 처리 없이 계속 진행.
			if (auto nonBlocking = SetNonBlocking(true); nonBlocking.IsError())
			{
				lastError = std::move(nonBlocking.Error());
				continue;
			}

			const auto& candidate = _candidates[i];
			if (::connect(handle.Get(), reinterpret_cast<const sockaddr*>(&candidate), candidate.ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in)) == 0)
				co_return ne::Result<void, ne::OsError>::Ok(); // 즉시 연결(로컬 루프백 등 드문 케이스)

			const ulong_t connectError = LastOsError();
	#if defined(_WIN32)
			const bool_t inProgress = (connectError == WSAEWOULDBLOCK);
	#elif defined(IS_POSIX)
			const bool_t inProgress = (connectError == EINPROGRESS);
	#endif

			if (!inProgress)
			{
				lastError = ne::OsError{ connectError };
				continue;
			}

			// 연결 완료(성공/실패 모두) 대기 — writable 또는 에러 이벤트. Send()/Sendv() 가 이미
			// 쓰는 것과 동일한 Awaitable 이라 Epoll/IoUring/Iocp 세 엔진 모두 균일하게 지원된다.
			if (auto waited = co_await ne::io::SendAwaitable{ handle.Get(), _engine }; waited.IsError())
			{
				lastError = std::move(waited.Error());
				continue;
			}

			// writable 이벤트 자체는 연결 성공/실패 모두에서 발생할 수 있으므로 SO_ERROR 로
			// 실제 결과를 확정해야 한다.
			int_t soError = 0;
			socklen_t soErrorLen = sizeof(soError);
			::getsockopt(handle.Get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char_t*>(&soError), &soErrorLen);

			if (soError == 0)
				co_return ne::Result<void, ne::OsError>::Ok();

			lastError = ne::OsError{ static_cast<ulong_t>(soError) };
		}

		co_return ne::Result<void, ne::OsError>::Error(lastError.Context("[Socket/Connect]"));
	}

END_NS
