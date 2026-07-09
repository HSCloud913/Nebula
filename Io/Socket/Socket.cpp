//
// Created by hscloud on 26. 7. 8.
//

#include "Io/Socket/Socket.h"

#include <utility>
#include "Io/Context/Context.h"
#include "Io/Coroutine/Awaitable.h"
#include "Base/Error.h"

#if defined(_WIN32)
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <mstcpip.h>
#elif defined(IS_POSIX)
#   include <unistd.h>
#   include <arpa/inet.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <sys/socket.h>
#endif



BEGIN_NS(ne::io)
	inline bool_t ParseAddress(string_view_t _ip, uint16_t _port, sockaddr_storage& _out, int_t& _length)
	{
		const string_t ip{ _ip };

		auto* v4 = reinterpret_cast<sockaddr_in*>(&_out);
		if (::inet_pton(AF_INET, ip.c_str(), &v4->sin_addr) == 1)
		{
			v4->sin_family = AF_INET;
			v4->sin_port = ::htons(_port);
			_length = static_cast<int_t>(sizeof(sockaddr_in));
			return true;
		}

		auto* v6 = reinterpret_cast<sockaddr_in6*>(&_out);
		if (::inet_pton(AF_INET6, ip.c_str(), &v6->sin6_addr) == 1)
		{
			v6->sin6_family = AF_INET6;
			v6->sin6_port = ::htons(_port);
			_length = static_cast<int_t>(sizeof(sockaddr_in6));
			return true;
		}

		return false;
	};



	Socket::Socket(SocketHandle&& _handle, Context& _context) noexcept
		: handle(std::move(_handle))
		, context(&_context) {}



	IoResult<Socket> Socket::Create(Context& _context, const int_t _family, const int_t _type, const int_t _protocol)
	{
#if defined(_WIN32)
		// Connect/Accept 와 동일하게 항상 RIO 를 켜둔다(생성 시점에만 지정 가능하므로).
		const socket_t raw = static_cast<socket_t>(::WSASocketW(_family, _type, _protocol, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO));
#elif defined(IS_POSIX)
		const socket_t raw = ::socket(_family, _type, _protocol);
#endif
		if (raw == InvalidSocket) return IoResult<Socket>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/Create]"));

		return IoResult<Socket>::Ok(Socket{ SocketHandle{ raw }, _context });
	}

	IoResult<Socket> Socket::Attach(const socket_t _handle, Context& _context)
	{
		if (_handle == InvalidSocket) return IoResult<Socket>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid socket handle" }.Context("[Socket/Adopt]"));

		return IoResult<Socket>::Ok(Socket{ SocketHandle{ _handle }, _context });
	}



	ne::Result<void_t, IoError> Socket::Bind(const string_view_t _ip, const uint16_t _port)
	{
		sockaddr_storage address{};
		int_t addressLength = 0;
		if (!ParseAddress(_ip, _port, address, addressLength)) return ne::Result<void_t, IoError>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid ip" }.Context("[Socket/Bind]"));

		if (::bind(handle.Get(), reinterpret_cast<sockaddr*>(&address), addressLength) != 0) return ne::Result<void_t, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/Bind]"));

		return ne::Result<void_t, IoError>::Ok();
	}

	ne::Result<void_t, IoError> Socket::Listen(const int_t _backlog)
	{
		if (::listen(handle.Get(), _backlog) != 0) return ne::Result<void_t, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/Listen]"));

		return ne::Result<void_t, IoError>::Ok();
	}

	ne::Task<IoResult<Socket>> Socket::Accept(std::stop_token _stopToken)
	{
		auto result = co_await Awaitable{ *context, Request{
			.op = OpCode::Accept,
			.handle = static_cast<ulonglong_t>(handle.Get()) },
		std::move(_stopToken) };
		if (result.IsError()) co_return IoResult<Socket>::Error(std::move(result.Error()).Context("[Socket/Accept]"));

		// 성공 시 완료 result 값은 accept 소켓 핸들.
		co_return Attach(static_cast<socket_t>(result.Value()), *context);
	}


	ne::Task<ne::Result<void_t, IoError>> Socket::Connect(string_view_t _ip, const uint16_t _port, std::stop_token _stopToken)
	{
		sockaddr_storage address{};
		int_t addressLength = 0;
		if (!ParseAddress(_ip, _port, address, addressLength)) co_return ne::Result<void_t, IoError>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid ip" }.Context("[Socket/Connect]"));

		auto result = co_await Awaitable{ *context, Request{
											.op = OpCode::Connect,
											.handle = static_cast<ulonglong_t>(handle.Get()),
											.address = &address,
											.addressLength = addressLength },
										std::move(_stopToken) };
		if (result.IsError()) co_return ne::Result<void_t, IoError>::Error(std::move(result.Error()).Context("[Socket/Connect]"));

		co_return ne::Result<void_t, IoError>::Ok();
	}


	ne::Result<void_t, IoError> Socket::Close()
	{
		handle = SocketHandle{}; // 기존 소켓 silently close 후 무효화
		return ne::Result<void_t, IoError>::Ok();
	}



	ne::Task<IoResult<std::size_t>> Socket::Receive(std::span<ne::byte_t> _buffer, std::stop_token _stopToken)
	{
		co_return co_await Awaitable{ *context, Request{
										.op = OpCode::Receive,
										.handle = static_cast<ulonglong_t>(handle.Get()),
										.buffer = _buffer.data(),
										.length = _buffer.size() },
									std::move(_stopToken) };
	}

	ne::Task<IoResult<std::size_t>> Socket::Send(std::span<const ne::byte_t> _buffer, std::stop_token _stopToken)
	{
		co_return co_await Awaitable{ *context, Request{
										.op = OpCode::Send,
										.handle = static_cast<ulonglong_t>(handle.Get()),
										.buffer = const_cast<ne::byte_t*>(_buffer.data()),
										.length = _buffer.size() },
									std::move(_stopToken) };
	}


	ne::Task<IoResult<std::size_t>> Socket::Receivev(const BufferChain& _chain, std::stop_token _stopToken)
	{
		co_return co_await Awaitable{ *context, Request{
										.op = OpCode::Receive,
										.handle = static_cast<ulonglong_t>(handle.Get()),
										.length = _chain.TotalSize(),
										.chain = &_chain },
									std::move(_stopToken) };
	}

	ne::Task<IoResult<std::size_t>> Socket::Sendv(const BufferChain& _chain, std::stop_token _stopToken)
	{
		co_return co_await Awaitable{ *context, Request{
										.op = OpCode::Send,
										.handle = static_cast<ulonglong_t>(handle.Get()),
										.length = _chain.TotalSize(),
										.chain = &_chain },
									std::move(_stopToken) };
	}


	ne::Task<IoResult<std::size_t>> Socket::SendZeroCopy(const BufferHandle _handle, std::span<const ne::byte_t> _buffer, std::stop_token _stopToken)
	{
		co_return co_await Awaitable{ *context, Request{
										.op = OpCode::SendZeroCopy,
										.handle = static_cast<ulonglong_t>(handle.Get()),
										.buffer = const_cast<ne::byte_t*>(_buffer.data()),
										.length = _buffer.size(),
										.bufferId = _handle.value },
									std::move(_stopToken) };
	}

	ne::Task<IoResult<std::size_t>> Socket::SendFile(const file_t _file, const ulonglong_t _offset, const std::size_t _length, std::stop_token _stopToken)
	{
#if defined(_WIN32)
		const ulonglong_t auxHandle = reinterpret_cast<ulonglong_t>(_file);
#elif defined(IS_POSIX)
		const ulonglong_t auxHandle = static_cast<ulonglong_t>(_file);
#endif

		co_return co_await Awaitable{ *context, Request{
										.op = OpCode::SendFile,
										.handle = static_cast<ulonglong_t>(handle.Get()),
										.length = _length,
										.offset = _offset,
										.auxHandle = auxHandle },
									std::move(_stopToken) };
	}


	ne::Task<IoResult<std::size_t>> Socket::SendTo(std::span<const ne::byte_t> _buffer, const string_view_t _ip, const uint16_t _port, std::stop_token _stopToken)
	{
		sockaddr_storage address{};
		int_t addressLength = 0;
		if (!ParseAddress(_ip, _port, address, addressLength)) co_return IoResult<std::size_t>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid ip" }.Context("[Socket/SendTo]"));

		co_return co_await Awaitable{ *context, Request{
										.op = OpCode::SendTo,
										.handle = static_cast<ulonglong_t>(handle.Get()),
										.buffer = const_cast<ne::byte_t*>(_buffer.data()),
										.length = _buffer.size(),
										.address = &address,
										.addressLength = addressLength },
									std::move(_stopToken) };
	}

	ne::Task<IoResult<std::size_t>> Socket::ReceiveFrom(std::span<ne::byte_t> _buffer, string_t& _ip, uint16_t& _port, std::stop_token _stopToken)
	{
		sockaddr_storage fromAddress{};
		int_t fromAddressLength = static_cast<int_t>(sizeof(fromAddress));

		auto result = co_await Awaitable{ *context, Request{
											.op = OpCode::ReceiveFrom,
											.handle = static_cast<ulonglong_t>(handle.Get()),
											.buffer = _buffer.data(),
											.length = _buffer.size(),
											.fromAddress = &fromAddress,
											.fromAddressLength = &fromAddressLength },
										std::move(_stopToken) };
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



	ne::Result<void_t, IoError> Socket::SetReuseAddress(const bool_t _enable)
	{
		const int_t value = _enable ? 1 : 0;
		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char_t*>(&value), sizeof(value)) != 0)
			return ne::Result<void_t, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/SetReuseAddress]"));

		return ne::Result<void_t, IoError>::Ok();
	}

	ne::Result<void_t, IoError> Socket::SetNoDelay(const bool_t _enable)
	{
		const int_t value = _enable ? 1 : 0;
		if (::setsockopt(handle.Get(), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char_t*>(&value), sizeof(value)) != 0)
			return ne::Result<void_t, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/SetNoDelay]"));

		return ne::Result<void_t, IoError>::Ok();
	}

END_NS
