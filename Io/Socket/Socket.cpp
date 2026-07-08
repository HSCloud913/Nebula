//
// Created by hscloud on 26. 7. 8.
//

#include "Socket.h"

#include <utility>
#include "Context/IoContext.h"
#include "Coroutine/Awaitable.h"
#include "Error.h"

#if defined(_WIN32)
#   include <winsock2.h>
#   include <ws2tcpip.h>
#elif defined(IS_POSIX)
#   include <unistd.h>
#   include <arpa/inet.h>
#   include <netinet/in.h>
#endif

BEGIN_NS(ne::io)
	namespace detail
	{
		void_t CloseSocketHandle(const socket_t _handle) noexcept
		{
#if defined(_WIN32)
			::closesocket(_handle);
#elif defined(IS_POSIX)
			::close(_handle);
#endif
		}

		bool_t ParseEndpoint(const Endpoint& _endpoint, sockaddr_storage& _out, int_t& _length) noexcept
		{
			const string_t ip{ _endpoint.ip };

			auto* v4 = reinterpret_cast<sockaddr_in*>(&_out);
			if (::inet_pton(AF_INET, ip.c_str(), &v4->sin_addr) == 1)
			{
				v4->sin_family = AF_INET;
				v4->sin_port = ::htons(_endpoint.port);
				_length = static_cast<int_t>(sizeof(sockaddr_in));
				return true;
			}

			auto* v6 = reinterpret_cast<sockaddr_in6*>(&_out);
			if (::inet_pton(AF_INET6, ip.c_str(), &v6->sin6_addr) == 1)
			{
				v6->sin6_family = AF_INET6;
				v6->sin6_port = ::htons(_endpoint.port);
				_length = static_cast<int_t>(sizeof(sockaddr_in6));
				return true;
			}

			return false;
		}

		PeerAddress FormatAddress(const sockaddr_storage& _address) noexcept
		{
			char_t buffer[INET6_ADDRSTRLEN]{};
			PeerAddress result;

			if (_address.ss_family == AF_INET)
			{
				const auto* v4 = reinterpret_cast<const sockaddr_in*>(&_address);
				::inet_ntop(AF_INET, &v4->sin_addr, buffer, sizeof(buffer));
				result.port = ::ntohs(v4->sin_port);
			}
			else if (_address.ss_family == AF_INET6)
			{
				const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&_address);
				::inet_ntop(AF_INET6, &v6->sin6_addr, buffer, sizeof(buffer));
				result.port = ::ntohs(v6->sin6_port);
			}

			result.ip = buffer;
			return result;
		}
	}

	namespace
	{
		[[nodiscard]] ulonglong_t ToHandleValue(const socket_t _handle) noexcept
		{
			return static_cast<ulonglong_t>(_handle);
		}
	}

	Socket::Socket(SocketHandle&& _handle, IoContext& _context) noexcept
		: handle(std::move(_handle))
		, context(&_context) {}

	IoResult<Socket> Socket::Adopt(IoContext& _context, const socket_t _handle)
	{
		if (_handle == InvalidSocket)
			return IoResult<Socket>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid socket handle" }.Context("[Socket/Adopt]"));

		return IoResult<Socket>::Ok(Socket{ SocketHandle{ _handle }, _context });
	}

	ne::Task<IoResult<Socket>> Socket::Connect(IoContext& _context, Endpoint _endpoint, const int_t _type, const int_t _protocol)
	{
		sockaddr_storage address{};
		int_t addressLength = 0;
		if (!detail::ParseEndpoint(_endpoint, address, addressLength))
			co_return IoResult<Socket>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid endpoint ip" }.Context("[Socket/Connect]"));

#if defined(_WIN32)
		const socket_t raw = static_cast<socket_t>(::WSASocketW(address.ss_family, _type, _protocol, nullptr, 0, WSA_FLAG_OVERLAPPED));
#elif defined(IS_POSIX)
		const socket_t raw = ::socket(address.ss_family, _type, _protocol);
#endif
		if (raw == InvalidSocket)
			co_return IoResult<Socket>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Socket/Connect socket]"));

		SocketHandle socketHandle{ raw }; // 프레임이 소유 — 에러 시 소멸자가 닫고, 성공 시 Socket 으로 이동

		auto result = co_await Awaitable{ _context, IoRequest{
			.op = OpCode::Connect, .handle = ToHandleValue(raw),
			.address = &address, .addressLength = addressLength } };
		if (result.IsError())
			co_return IoResult<Socket>::Error(std::move(result.Error()).Context("[Socket/Connect]"));

		co_return IoResult<Socket>::Ok(Socket{ std::move(socketHandle), _context });
	}

	ne::Task<IoResult<Socket>> Socket::Accept()
	{
		auto result = co_await Awaitable{ *context, IoRequest{ .op = OpCode::Accept, .handle = ToHandleValue(handle.Get()) } };
		if (result.IsError())
			co_return IoResult<Socket>::Error(std::move(result.Error()).Context("[Socket/Accept]"));

		// 성공 시 완료 result 값은 accept 소켓 핸들.
		co_return Socket::Adopt(*context, static_cast<socket_t>(result.Value()));
	}

	ne::Task<IoResult<std::size_t>> Socket::Receive(std::span<ne::byte_t> _buffer)
	{
		co_return co_await Awaitable{ *context, IoRequest{
			.op = OpCode::Receive, .handle = ToHandleValue(handle.Get()),
			.buffer = _buffer.data(), .length = _buffer.size() } };
	}

	ne::Task<IoResult<std::size_t>> Socket::Send(std::span<const ne::byte_t> _buffer)
	{
		co_return co_await Awaitable{ *context, IoRequest{
			.op = OpCode::Send, .handle = ToHandleValue(handle.Get()),
			.buffer = const_cast<ne::byte_t*>(_buffer.data()), .length = _buffer.size() } };
	}

	ne::Task<IoResult<std::size_t>> Socket::Receivev(const BufferChain& _chain)
	{
		co_return co_await Awaitable{ *context, IoRequest{
			.op = OpCode::Receive, .handle = ToHandleValue(handle.Get()),
			.length = _chain.TotalSize(), .chain = &_chain } };
	}

	ne::Task<IoResult<std::size_t>> Socket::Sendv(const BufferChain& _chain)
	{
		co_return co_await Awaitable{ *context, IoRequest{
			.op = OpCode::Send, .handle = ToHandleValue(handle.Get()),
			.length = _chain.TotalSize(), .chain = &_chain } };
	}

	ne::Task<IoResult<std::size_t>> Socket::SendTo(std::span<const ne::byte_t> _buffer, const Endpoint _destination)
	{
		sockaddr_storage address{};
		int_t addressLength = 0;
		if (!detail::ParseEndpoint(_destination, address, addressLength))
			co_return IoResult<std::size_t>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "invalid endpoint ip" }.Context("[Socket/SendTo]"));

		co_return co_await Awaitable{ *context, IoRequest{
			.op = OpCode::SendTo, .handle = ToHandleValue(handle.Get()),
			.buffer = const_cast<ne::byte_t*>(_buffer.data()), .length = _buffer.size(),
			.address = &address, .addressLength = addressLength } };
	}

	ne::Task<IoResult<ReceiveFromResult>> Socket::ReceiveFrom(std::span<ne::byte_t> _buffer)
	{
		sockaddr_storage fromAddress{};
		int_t fromAddressLength = static_cast<int_t>(sizeof(fromAddress));

		auto result = co_await Awaitable{ *context, IoRequest{
			.op = OpCode::ReceiveFrom, .handle = ToHandleValue(handle.Get()),
			.buffer = _buffer.data(), .length = _buffer.size(),
			.fromAddress = &fromAddress, .fromAddressLength = &fromAddressLength } };
		if (result.IsError())
			co_return IoResult<ReceiveFromResult>::Error(std::move(result.Error()).Context("[Socket/ReceiveFrom]"));

		co_return IoResult<ReceiveFromResult>::Ok(ReceiveFromResult{ result.Value(), detail::FormatAddress(fromAddress) });
	}

	ne::Result<void_t, IoError> Socket::Close()
	{
		handle = SocketHandle{}; // 기존 소켓 silently close 후 무효화
		return ne::Result<void_t, IoError>::Ok();
	}

END_NS
