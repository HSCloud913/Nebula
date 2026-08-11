//
// Created by hscloud on 26. 7. 8.
//

#include "Io/Socket.h"

#include <utility>
#include "Io/Context.h"
#include "Io/Coroutine/Awaitable.h"
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

		auto result = co_await Awaitable{ *context, request, std::move(_stopToken) };
		if (result.IsError()) co_return IoResult<Socket>::Error(std::move(result.Error()).Context("[Socket/Accept]"));

		co_return Attach(static_cast<socket_t>(result.Value()), *context, _isRegisteredIo);
	}



	ne::Task<IoResult<void_t>> Socket::Connect(string_view_t _ip, const uint16_t _port, std::stop_token _stopToken)
	{
		sockaddr_storage address{};
		int_t addressLength = 0;
		if (!ParseAddress(_ip, _port, address, addressLength)) co_return IoResult<void_t>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid ip" }.Context("[Socket/Connect]"));

		const Request request{ .requestKind = RequestKind::CONNECT, .handle = static_cast<ulonglong_t>(handle.Get()), .address = &address, .addressLength = addressLength };

		if (auto result = co_await Awaitable{ *context, request, std::move(_stopToken) }; result.IsError()) co_return IoResult<void_t>::Error(std::move(result.Error()).Context("[Socket/Connect]"));

		co_return IoResult<void_t>::Ok();
	}



	ne::Task<IoResult<std::size_t>> Socket::Receive(std::span<ne::byte_t> _buffer, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::RECEIVE, .handle = static_cast<ulonglong_t>(handle.Get()), .buffer = _buffer.data(), .length = _buffer.size() };

		auto result = co_await Awaitable{ *context, request, std::move(_stopToken) };
		co_return result;
	}

	ne::Task<IoResult<std::size_t>> Socket::Send(std::span<const ne::byte_t> _buffer, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::SEND, .handle = static_cast<ulonglong_t>(handle.Get()), .buffer = const_cast<ne::byte_t*>(_buffer.data()), .length = _buffer.size() };

		auto result = co_await Awaitable{ *context, request, std::move(_stopToken) };
		co_return result;
	}


	ne::Task<IoResult<std::size_t>> Socket::Receivev(const ne::memory::BufferChain& _chain, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::RECEIVE, .handle = static_cast<ulonglong_t>(handle.Get()), .length = _chain.TotalSize(), .chain = &_chain };

		auto result = co_await Awaitable{ *context, request, std::move(_stopToken) };
		co_return result;
	}

	ne::Task<IoResult<std::size_t>> Socket::Sendv(const ne::memory::BufferChain& _chain, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::SEND, .handle = static_cast<ulonglong_t>(handle.Get()), .length = _chain.TotalSize(), .chain = &_chain };

		auto result = co_await Awaitable{ *context, request, std::move(_stopToken) };
		co_return result;
	}


	ne::Task<IoResult<void_t>> Socket::WaitReadable(std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::WAIT_READABLE, .handle = static_cast<ulonglong_t>(handle.Get()) };

		if (auto result = co_await Awaitable{ *context, request, std::move(_stopToken) }; result.IsError()) co_return IoResult<void_t>::Error(std::move(result.Error()).Context("[Socket/WaitReadable]"));

		co_return IoResult<void_t>::Ok();
	}

	ne::Task<IoResult<void_t>> Socket::WaitWritable(std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::WAIT_WRITABLE, .handle = static_cast<ulonglong_t>(handle.Get()) };

		if (auto result = co_await Awaitable{ *context, request, std::move(_stopToken) }; result.IsError()) co_return IoResult<void_t>::Error(std::move(result.Error()).Context("[Socket/WaitWritable]"));

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

		auto result = co_await Awaitable{ *context, request, std::move(_stopToken) };
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

		auto result = co_await Awaitable{ *context, request, std::move(_stopToken) };
		co_return result;
	}


	ne::Task<IoResult<std::size_t>> Socket::SendTo(std::span<const ne::byte_t> _buffer, const string_view_t _ip, const uint16_t _port, std::stop_token _stopToken)
	{
		sockaddr_storage address{};
		int_t addressLength = 0;
		if (!ParseAddress(_ip, _port, address, addressLength)) co_return IoResult<std::size_t>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid ip" }.Context("[Socket/SendTo]"));

		const Request request{ .requestKind = RequestKind::SEND_TO, .handle = static_cast<ulonglong_t>(handle.Get()), .buffer = const_cast<ne::byte_t*>(_buffer.data()), .length = _buffer.size(), .address = &address,
								.addressLength = addressLength };

		auto result = co_await Awaitable{ *context, request, std::move(_stopToken) };
		co_return result;
	}

	ne::Task<IoResult<std::size_t>> Socket::ReceiveFrom(std::span<ne::byte_t> _buffer, string_t& _ip, uint16_t& _port, std::stop_token _stopToken)
	{
		sockaddr_storage fromAddress{};
		auto fromAddressLength = static_cast<int_t>(sizeof(fromAddress));

		const Request request{ .requestKind = RequestKind::RECEIVE_FROM, .handle = static_cast<ulonglong_t>(handle.Get()), .buffer = _buffer.data(), .length = _buffer.size(), .fromAddress = &fromAddress,
								.fromAddressLength = &fromAddressLength };

		auto result = co_await Awaitable{ *context, request, std::move(_stopToken) };
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



	IoResult<void_t> Socket::SetReuseAddress(const bool_t _enable)
	{
		const int_t value = _enable ? 1 : 0;
		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char_t*>(&value), sizeof(value)) != 0) return IoResult<void_t>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/SetReuseAddress]"));

		return IoResult<void_t>::Ok();
	}

	IoResult<void_t> Socket::SetNoDelay(const bool_t _enable)
	{
		const int_t value = _enable ? 1 : 0;
		if (::setsockopt(handle.Get(), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char_t*>(&value), sizeof(value)) != 0) return IoResult<void_t>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/SetNoDelay]"));

		return IoResult<void_t>::Ok();
	}



	IoResult<void_t> Socket::Shutdown()
	{
#if defined(_WIN32)
		constexpr int_t how = SD_SEND;
#elif defined(IS_POSIX)
		constexpr int_t how = SHUT_WR;
#endif
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
