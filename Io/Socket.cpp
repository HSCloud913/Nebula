//
// Created by hscloud on 26. 7. 8.
//

#include "Io/Socket.h"

#include <utility>
#include "Io/Context.h"
#include "Io/Coroutine/IoOperation.h"
#include "Base/Error.h"

#if defined(_WIN32)
#include "Base/WinsockApi.h"
#   include <mstcpip.h>
#elif defined(IS_POSIX)
#   include <unistd.h>
#   include <fcntl.h>
#   include <arpa/inet.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <sys/socket.h>
#endif



// CompletionHandler::addressStorage 는 <winsock2.h>/<sys/socket.h> 를 Context.h 로 끌어오지 않기 위해
// 원시 바이트 배열로 선언되어 있다. 실제 sockaddr_storage 와 크기·정렬이 맞는지 여기서 못박는다.
static_assert(sizeof(sockaddr_storage) <= ne::io::CompletionHandler::AddressStorageSize, "CompletionHandler::addressStorage 가 sockaddr_storage 보다 작다");
static_assert(alignof(sockaddr_storage) <= alignof(ne::io::CompletionHandler), "CompletionHandler 의 정렬이 sockaddr_storage 요구보다 약하다");

namespace ne::io
{
	namespace
	{
		ne::bool_t ParseAddress(ne::string_view_t _ip, uint16_t _port, sockaddr_storage& _out, ne::int_t& _length)
		{
			const ne::string_t ip{ _ip };

			auto* v4 = reinterpret_cast<sockaddr_in*>(&_out);
			if (::inet_pton(AF_INET, ip.c_str(), &v4->sin_addr) == 1)
			{
				v4->sin_family = AF_INET;
				v4->sin_port = ::htons(_port);
				_length = static_cast<ne::int_t>(sizeof(sockaddr_in));
				return true;
			}

			auto* v6 = reinterpret_cast<sockaddr_in6*>(&_out);
			if (::inet_pton(AF_INET6, ip.c_str(), &v6->sin6_addr) == 1)
			{
				v6->sin6_family = AF_INET6;
				v6->sin6_port = ::htons(_port);
				_length = static_cast<ne::int_t>(sizeof(sockaddr_in6));
				return true;
			}

			return false;
		}

		// 리액터 엔진(Epoll/WsaPoll)은 recv/accept/connect 가 EAGAIN 을 돌려준다는 전제로 만들어져 있다.
		// 소켓이 블로킹이면 그 호출들이 이벤트 루프 스레드를 붙잡아, REACTOR 엔진이 사실상 단일 연결
		// 동기 엔진으로 퇴화한다. 프로액터 엔진(IOCP/io_uring)은 중첩/제출 기반이라 논블로킹 여부에
		// 영향을 받지 않으므로, 엔진 종류를 따지지 않고 항상 논블로킹으로 만드는 것이 안전하다.
		//
		// @note 예외는 RIO(WSA_FLAG_REGISTERED_IO) 소켓이다. Winsock 이 그 소켓에 대해 FIONBIO 를
		//       WSAEOPNOTSUPP 으로 거부한다(실측). RIO 는 RIOSend/RIOReceive 로만 쓰이는 완전 비동기
		//       경로라 블로킹 모드 자체가 의미가 없으므로, 호출자가 건너뛴다.
		ne::bool_t SetNonBlocking(const socket_t _socket) noexcept
		{
#if defined(_WIN32)
			ne::ulong_t mode = 1;
			return ::ioctlsocket(_socket, FIONBIO, &mode) == 0;
#elif defined(IS_POSIX)
			const int flags = ::fcntl(_socket, F_GETFL, 0);
			if (flags < 0) return false;

			return ::fcntl(_socket, F_SETFL, flags | O_NONBLOCK) == 0;
#else
			(void)_socket;
			return true;
#endif
		}
	}



	Socket::Socket(SocketHandle&& _handle, Context& _context, const bool_t _isRegisteredIo) noexcept
		: handle(std::move(_handle))
		, context(&_context)
		, isRegisteredIo(_isRegisteredIo) {}

	Socket::~Socket() { DeregisterFromEngine(); }

	Socket& Socket::operator=(Socket&& _other) noexcept
	{
		if (this != &_other)
		{
			DeregisterFromEngine(); // 교체되며 닫힐 기존 핸들의 연관을 먼저 해제한다.
			handle = std::move(_other.handle);
			context = _other.context;
			isRegisteredIo = _other.isRegisteredIo;
		}

		return *this;
	}

	void_t Socket::DeregisterFromEngine() noexcept
	{
		if (context != nullptr && handle) context->Engine().Deregister(static_cast<ulonglong_t>(handle.Get()));
	}



	IoResult<Socket> Socket::Create(Context& _context, const int_t _family, const int_t _type, const int_t _protocol, const bool_t _isRegisteredIo)
	{
#if defined(_WIN32)
		const socket_t raw = ::WSASocketW(_family, _type, _protocol, nullptr, 0, WSA_FLAG_OVERLAPPED | (_isRegisteredIo ? WSA_FLAG_REGISTERED_IO : 0));
#elif defined(IS_POSIX)
		const socket_t raw = ::socket(_family, _type, _protocol);
#endif
		if (raw == InvalidSocket) return IoResult<Socket>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/Create]"));

		// 실패하면 소켓을 누출하지 않도록 SocketHandle 로 먼저 감싼 뒤 검사한다.
		SocketHandle handle{ raw };
		if (!_isRegisteredIo && !SetNonBlocking(raw)) return IoResult<Socket>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/Create/SetNonBlocking]"));

		return IoResult<Socket>::Ok(Socket{ std::move(handle), _context, _isRegisteredIo });
	}

	IoResult<Socket> Socket::Attach(const socket_t _handle, Context& _context, const bool_t _isRegisteredIo)
	{
		if (_handle == InvalidSocket) return IoResult<Socket>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid socket handle" }.Context("[Socket/Attach]"));

		// Accept 로 만들어진 소켓은 리스너의 블로킹 모드를 물려받지 않는(Windows) / 물려받는(Linux) 등
		// 플랫폼차가 있어, 인수한 모든 소켓에 대해 명시적으로 다시 설정한다.
		SocketHandle handle{ _handle };
		if (!_isRegisteredIo && !SetNonBlocking(_handle)) return IoResult<Socket>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/Attach/SetNonBlocking]"));

		return IoResult<Socket>::Ok(Socket{ std::move(handle), _context, _isRegisteredIo });
	}



	IoResult<void_t> Socket::Bind(const string_view_t _ip, const uint16_t _port)
	{
		sockaddr_storage address{};
		int_t addressLength = 0;
		if (!ParseAddress(_ip, _port, address, addressLength)) return IoResult<void_t>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid ip" }.Context("[Socket/Bind]"));

		if (::bind(handle.Get(), reinterpret_cast<sockaddr*>(&address), addressLength) != 0) return IoResult<void_t>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/Bind]"));

		return IoResult<void_t>::Ok();
	}

	IoResult<void_t> Socket::Listen(const int_t _backlog)
	{
		if (::listen(handle.Get(), _backlog) != 0) return IoResult<void_t>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/Listen]"));

		return IoResult<void_t>::Ok();
	}

	ne::Task<IoResult<Socket>> Socket::Accept(const bool_t _isRegisteredIo, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::ACCEPT, .handle = static_cast<ulonglong_t>(handle.Get()), .isRegisteredIo = _isRegisteredIo };

		auto result = co_await IoOperation{ *context, request, std::move(_stopToken) };
		if (result.IsError()) co_return IoResult<Socket>::Error(std::move(result.Error()).Context("[Socket/Accept]"));

		co_return Attach(static_cast<socket_t>(result.Value()), *context, _isRegisteredIo);
	}



	ne::Task<IoResult<void_t>> Socket::Connect(string_view_t _ip, const uint16_t _port, std::stop_token _stopToken)
	{
		sockaddr_storage address{};
		int_t addressLength = 0;
		if (!ParseAddress(_ip, _port, address, addressLength)) co_return IoResult<void_t>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid ip" }.Context("[Socket/Connect]"));

		const Request request{ .requestKind = RequestKind::CONNECT, .handle = static_cast<ulonglong_t>(handle.Get()), .address = &address, .addressLength = addressLength };

		if (auto result = co_await IoOperation{ *context, request, std::move(_stopToken) }; result.IsError()) co_return IoResult<void_t>::Error(std::move(result.Error()).Context("[Socket/Connect]"));

		co_return IoResult<void_t>::Ok();
	}



	ne::Task<IoResult<std::size_t>> Socket::Receive(std::span<ne::byte_t> _buffer, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::RECEIVE, .handle = static_cast<ulonglong_t>(handle.Get()), .buffer = _buffer.data(), .length = _buffer.size() };

		auto result = co_await IoOperation{ *context, request, std::move(_stopToken) };
		co_return result;
	}

	ne::Task<IoResult<std::size_t>> Socket::Send(std::span<const ne::byte_t> _buffer, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::SEND, .handle = static_cast<ulonglong_t>(handle.Get()), .buffer = const_cast<ne::byte_t*>(_buffer.data()), .length = _buffer.size() };

		auto result = co_await IoOperation{ *context, request, std::move(_stopToken) };
		co_return result;
	}


	ne::Task<IoResult<std::size_t>> Socket::Receivev(const ne::memory::BufferChain& _chain, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::RECEIVE, .handle = static_cast<ulonglong_t>(handle.Get()), .length = _chain.TotalSize(), .chain = &_chain };

		auto result = co_await IoOperation{ *context, request, std::move(_stopToken) };
		co_return result;
	}

	ne::Task<IoResult<std::size_t>> Socket::Sendv(const ne::memory::BufferChain& _chain, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::SEND, .handle = static_cast<ulonglong_t>(handle.Get()), .length = _chain.TotalSize(), .chain = &_chain };

		auto result = co_await IoOperation{ *context, request, std::move(_stopToken) };
		co_return result;
	}


	ne::Task<IoResult<void_t>> Socket::WaitReadable(std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::WAIT_READABLE, .handle = static_cast<ulonglong_t>(handle.Get()) };

		if (auto result = co_await IoOperation{ *context, request, std::move(_stopToken) }; result.IsError()) co_return IoResult<void_t>::Error(std::move(result.Error()).Context("[Socket/WaitReadable]"));

		co_return IoResult<void_t>::Ok();
	}

	ne::Task<IoResult<void_t>> Socket::WaitWritable(std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::WAIT_WRITABLE, .handle = static_cast<ulonglong_t>(handle.Get()) };

		if (auto result = co_await IoOperation{ *context, request, std::move(_stopToken) }; result.IsError()) co_return IoResult<void_t>::Error(std::move(result.Error()).Context("[Socket/WaitWritable]"));

		co_return IoResult<void_t>::Ok();
	}


	ne::Task<IoResult<std::size_t>> Socket::SendZeroCopy(const BufferHandle _handle, std::span<const ne::byte_t> _buffer, std::stop_token _stopToken)
	{
		if (!context->Engine().Supports(Capability::SEND_MEM_ZERO_COPY)) co_return IoResult<std::size_t>::Error(IoError{ IoErrorKind::UNSUPPORTED, "engine does not support zero-copy send" }.Context("[Socket/SendZeroCopy]"));

#if defined(_WIN32)
		if (!isRegisteredIo) co_return IoResult<std::size_t>::Error(IoError{ IoErrorKind::UNSUPPORTED, "socket is not registered-IO (use Create(..., true) or Accept(true))" }.Context("[Socket/SendZeroCopy]"));
#endif

		const Request request{ .requestKind = RequestKind::SEND_ZERO_COPY, .handle = static_cast<ulonglong_t>(handle.Get()), .buffer = const_cast<ne::byte_t*>(_buffer.data()), .length = _buffer.size(),
								.bufferId = _handle.value };

		auto result = co_await IoOperation{ *context, request, std::move(_stopToken) };
		co_return result;
	}

	ne::Task<IoResult<std::size_t>> Socket::SendFile(const file_t _file, const ulonglong_t _offset, const std::size_t _length, std::stop_token _stopToken)
	{
#if defined(_WIN32)
		const ulonglong_t auxHandle = reinterpret_cast<ulonglong_t>(_file);
#elif defined(IS_POSIX)
		const ulonglong_t auxHandle = static_cast<ulonglong_t>(_file);
#endif

		const Request request{ .requestKind = RequestKind::SEND_FILE, .handle = static_cast<ulonglong_t>(handle.Get()), .length = _length, .offset = _offset, .auxHandle = auxHandle };

		auto result = co_await IoOperation{ *context, request, std::move(_stopToken) };
		co_return result;
	}


	ne::Task<IoResult<std::size_t>> Socket::SendTo(std::span<const ne::byte_t> _buffer, const string_view_t _ip, const uint16_t _port, std::stop_token _stopToken)
	{
		sockaddr_storage address{};
		int_t addressLength = 0;
		if (!ParseAddress(_ip, _port, address, addressLength)) co_return IoResult<std::size_t>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid ip" }.Context("[Socket/SendTo]"));

		const Request request{ .requestKind = RequestKind::SEND_TO, .handle = static_cast<ulonglong_t>(handle.Get()), .buffer = const_cast<ne::byte_t*>(_buffer.data()), .length = _buffer.size(), .address = &address,
								.addressLength = addressLength };

		auto result = co_await IoOperation{ *context, request, std::move(_stopToken) };
		co_return result;
	}

	ne::Task<IoResult<std::size_t>> Socket::ReceiveFrom(std::span<ne::byte_t> _buffer, string_t& _ip, uint16_t& _port, std::stop_token _stopToken)
	{
		sockaddr_storage fromAddress{};
		auto fromAddressLength = static_cast<int_t>(sizeof(fromAddress));

		const Request request{ .requestKind = RequestKind::RECEIVE_FROM, .handle = static_cast<ulonglong_t>(handle.Get()), .buffer = _buffer.data(), .length = _buffer.size(), .fromAddress = &fromAddress,
								.fromAddressLength = &fromAddressLength };

		auto result = co_await IoOperation{ *context, request, std::move(_stopToken) };
		if (result.IsError()) co_return IoResult<std::size_t>::Error(std::move(result.Error()).Context("[Socket/ReceiveFrom]"));

		char_t buffer[INET6_ADDRSTRLEN]{};
		if (fromAddress.ss_family == AF_INET)
		{
			const auto* v4 = reinterpret_cast<const sockaddr_in*>(&fromAddress);
			::inet_ntop(AF_INET, &v4->sin_addr, buffer, sizeof(buffer));
			_port = ::ntohs(v4->sin_port);
		}
		else if (fromAddress.ss_family == AF_INET6)
		{
			const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&fromAddress);
			::inet_ntop(AF_INET6, &v6->sin6_addr, buffer, sizeof(buffer));
			_port = ::ntohs(v6->sin6_port);
		}

		_ip = buffer;

		co_return IoResult<std::size_t>::Ok(result.Value());
	}



	namespace
	{
		// 모든 옵션 setter 가 공유하는 원시 경로. 실패 시 어느 옵션이었는지 문맥에 남긴다.
		[[nodiscard]] IoResult<void_t> ApplyOption(const socket_t _socket, const int_t _level, const int_t _name, const void_t* _value, const int_t _length, const string_view_t _context)
		{
			if (::setsockopt(_socket, _level, _name, static_cast<const char_t*>(_value), _length) != 0) return IoResult<void_t>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context(_context));

			return IoResult<void_t>::Ok();
		}

		[[nodiscard]] IoResult<void_t> ApplyFlag(const socket_t _socket, const int_t _level, const int_t _name, const bool_t _isEnabled, const string_view_t _context)
		{
			const int_t value = _isEnabled ? 1 : 0;
			return ApplyOption(_socket, _level, _name, &value, static_cast<int_t>(sizeof(value)), _context);
		}

		// getsockname/getpeername 결과를 SocketAddress 로 정규화한다.
		[[nodiscard]] IoResult<SocketAddress> DescribeAddress(const sockaddr_storage& _address, const string_view_t _context)
		{
			char_t buffer[INET6_ADDRSTRLEN]{};
			SocketAddress result;
			result.family = _address.ss_family;

			if (_address.ss_family == AF_INET)
			{
				const auto* v4 = reinterpret_cast<const sockaddr_in*>(&_address);
				if (::inet_ntop(AF_INET, &v4->sin_addr, buffer, sizeof(buffer)) == nullptr) return IoResult<SocketAddress>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context(_context));
				result.port = ::ntohs(v4->sin_port);
			}
			else if (_address.ss_family == AF_INET6)
			{
				const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&_address);
				if (::inet_ntop(AF_INET6, &v6->sin6_addr, buffer, sizeof(buffer)) == nullptr) return IoResult<SocketAddress>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context(_context));
				result.port = ::ntohs(v6->sin6_port);
			}
			else
			{
				// AF_UNIX 등은 ip/port 개념이 없어 이 표현으로 담을 수 없다.
				return IoResult<SocketAddress>::Error(IoError{ IoErrorKind::UNSUPPORTED, "address family has no ip/port representation" }.Context(_context));
			}

			result.ip = buffer;

			return IoResult<SocketAddress>::Ok(std::move(result));
		}
	}



	IoResult<void_t> Socket::SetReuseAddress(const bool_t _isEnabled) { return ApplyFlag(handle.Get(), SOL_SOCKET, SO_REUSEADDR, _isEnabled, "[Socket/SetReuseAddress]"); }

	IoResult<void_t> Socket::SetNoDelay(const bool_t _isEnabled) { return ApplyFlag(handle.Get(), IPPROTO_TCP, TCP_NODELAY, _isEnabled, "[Socket/SetNoDelay]"); }

	IoResult<void_t> Socket::SetReusePort(const bool_t _isEnabled)
	{
#if defined(SO_REUSEPORT)
		return ApplyFlag(handle.Get(), SOL_SOCKET, SO_REUSEPORT, _isEnabled, "[Socket/SetReusePort]");
#else
		// Windows 에는 대응 옵션이 없다. SO_REUSEADDR 로 조용히 대체하면 의미가 달라(그쪽은 연결 분산이
		// 아니라 주소 재사용) 사용자가 없는 기능을 있다고 믿게 되므로 명시적으로 실패시킨다.
		(void_t)_isEnabled;
		return IoResult<void_t>::Error(IoError{ IoErrorKind::UNSUPPORTED, "SO_REUSEPORT is not available on this platform" }.Context("[Socket/SetReusePort]"));
#endif
	}

	IoResult<void_t> Socket::SetKeepAlive(const bool_t _isEnabled) { return ApplyFlag(handle.Get(), SOL_SOCKET, SO_KEEPALIVE, _isEnabled, "[Socket/SetKeepAlive]"); }

	IoResult<void_t> Socket::SetKeepAliveTiming(const std::chrono::seconds _idle, const std::chrono::seconds _interval)
	{
		if (_idle.count() <= 0 || _interval.count() <= 0) return IoResult<void_t>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "keepalive idle/interval must be positive" }.Context("[Socket/SetKeepAliveTiming]"));

#if defined(_WIN32)
		// Windows 는 개별 setsockopt 이 아니라 ioctl 한 번으로 idle/interval 을 함께 넘긴다(단위: ms).
		tcp_keepalive settings{};
		settings.onoff = 1;
		settings.keepalivetime = static_cast<ulong_t>(_idle.count() * 1000);
		settings.keepaliveinterval = static_cast<ulong_t>(_interval.count() * 1000);

		ulong_t returned = 0;
		if (::WSAIoctl(handle.Get(), SIO_KEEPALIVE_VALS, &settings, sizeof(settings), nullptr, 0, &returned, nullptr, nullptr) != 0) return IoResult<void_t>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/SetKeepAliveTiming]"));

		return IoResult<void_t>::Ok();
#elif defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL)
		const auto idle = static_cast<int_t>(_idle.count());
		if (auto result = ApplyOption(handle.Get(), IPPROTO_TCP, TCP_KEEPIDLE, &idle, static_cast<int_t>(sizeof(idle)), "[Socket/SetKeepAliveTiming/idle]"); result.IsError()) return result;

		const auto interval = static_cast<int_t>(_interval.count());
		return ApplyOption(handle.Get(), IPPROTO_TCP, TCP_KEEPINTVL, &interval, static_cast<int_t>(sizeof(interval)), "[Socket/SetKeepAliveTiming/interval]");
#else
		return IoResult<void_t>::Error(IoError{ IoErrorKind::UNSUPPORTED, "keepalive timing is not tunable on this platform" }.Context("[Socket/SetKeepAliveTiming]"));
#endif
	}

	IoResult<void_t> Socket::SetLinger(const LingerOption _option)
	{
		::linger value{};
		value.l_onoff = _option.isEnabled ? 1 : 0;
		value.l_linger = static_cast<decltype(value.l_linger)>(_option.seconds);

		return ApplyOption(handle.Get(), SOL_SOCKET, SO_LINGER, &value, static_cast<int_t>(sizeof(value)), "[Socket/SetLinger]");
	}

	IoResult<void_t> Socket::SetReceiveBufferSize(const int_t _bytes) { return ApplyOption(handle.Get(), SOL_SOCKET, SO_RCVBUF, &_bytes, static_cast<int_t>(sizeof(_bytes)), "[Socket/SetReceiveBufferSize]"); }

	IoResult<void_t> Socket::SetSendBufferSize(const int_t _bytes) { return ApplyOption(handle.Get(), SOL_SOCKET, SO_SNDBUF, &_bytes, static_cast<int_t>(sizeof(_bytes)), "[Socket/SetSendBufferSize]"); }

	IoResult<void_t> Socket::SetBroadcast(const bool_t _isEnabled) { return ApplyFlag(handle.Get(), SOL_SOCKET, SO_BROADCAST, _isEnabled, "[Socket/SetBroadcast]"); }

	IoResult<void_t> Socket::SetIpV6Only(const bool_t _isEnabled) { return ApplyFlag(handle.Get(), IPPROTO_IPV6, IPV6_V6ONLY, _isEnabled, "[Socket/SetIpV6Only]"); }

	IoResult<void_t> Socket::SetRawOption(const int_t _level, const int_t _name, const std::span<const ne::byte_t> _value)
	{
		return ApplyOption(handle.Get(), _level, _name, _value.data(), static_cast<int_t>(_value.size()), "[Socket/SetRawOption]");
	}



	uint16_t Socket::LocalPort() const noexcept
	{
		sockaddr_storage address{};
#if defined(_WIN32)
		int length = static_cast<int>(sizeof(address));
#else
		socklen_t length = static_cast<socklen_t>(sizeof(address));
#endif
		if (::getsockname(handle.Get(), reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0;

		if (address.ss_family == AF_INET6) return ::ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);

		return ::ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
	}

	IoResult<SocketAddress> Socket::LocalAddress() const
	{
		sockaddr_storage address{};
#if defined(_WIN32)
		int length = static_cast<int>(sizeof(address));
#else
		socklen_t length = static_cast<socklen_t>(sizeof(address));
#endif
		if (::getsockname(handle.Get(), reinterpret_cast<sockaddr*>(&address), &length) != 0) return IoResult<SocketAddress>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/LocalAddress]"));

		return DescribeAddress(address, "[Socket/LocalAddress]");
	}

	IoResult<SocketAddress> Socket::PeerAddress() const
	{
		sockaddr_storage address{};
#if defined(_WIN32)
		int length = static_cast<int>(sizeof(address));
#else
		socklen_t length = static_cast<socklen_t>(sizeof(address));
#endif
		if (::getpeername(handle.Get(), reinterpret_cast<sockaddr*>(&address), &length) != 0) return IoResult<SocketAddress>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/PeerAddress]"));

		return DescribeAddress(address, "[Socket/PeerAddress]");
	}



	IoResult<void_t> Socket::Shutdown(const ShutdownMode _mode)
	{
#if defined(_WIN32)
		constexpr int_t sendOnly = SD_SEND;
		constexpr int_t receiveOnly = SD_RECEIVE;
		constexpr int_t both = SD_BOTH;
#elif defined(IS_POSIX)
		constexpr int_t sendOnly = SHUT_WR;
		constexpr int_t receiveOnly = SHUT_RD;
		constexpr int_t both = SHUT_RDWR;
#endif
		const int_t how = _mode == ShutdownMode::SEND ? sendOnly : (_mode == ShutdownMode::RECEIVE ? receiveOnly : both);

		if (::shutdown(handle.Get(), how) != 0) return IoResult<void_t>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/Shutdown]"));

		return IoResult<void_t>::Ok();
	}


	IoResult<void_t> Socket::Close()
	{
		DeregisterFromEngine();
		handle = SocketHandle{};
		return IoResult<void_t>::Ok();
	}
}
