//
// Created by hscloud on 25. 6. 29.
//

#include "PlainStream.h"
#include "Coroutine/Awaitable.h"

#if defined(_WIN32)
#   include <winsock2.h>
#elif defined(IS_POSIX)
#   include <sys/socket.h>
#endif



BEGIN_NS(ne::network)
	PlainStream::PlainStream(PlainStream&& _other) noexcept
		: socket(std::move(_other.socket))
		, engine(_other.engine) {}

	PlainStream& PlainStream::operator=(PlainStream&& _other) noexcept
	{
		if (this != &_other)
		{
			socket = std::move(_other.socket);
			engine = _other.engine;
		}
		return *this;
	}

	PlainStream::PlainStream(Socket&& _socket, IIoEngine& _engine) noexcept
		: socket(std::move(_socket))
		, engine(&_engine) {}



	ne::Result<PlainStream, ne::OsError> PlainStream::Create(Socket&& _socket, IIoEngine& _engine) noexcept
	{
		if (!_socket.IsValid()) return ne::Result<PlainStream, ne::OsError>::Error(ne::OsError{ 0, "socket is not valid" });

		return ne::Result<PlainStream, ne::OsError>::Ok(PlainStream{ std::move(_socket), _engine });
	}



	ne::Task<ne::Result<void, ne::OsError>> PlainStream::Handshake()
	{
		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> PlainStream::Send(std::span<const ne::byte_t> _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "stream is closed" });

		if (auto ready = co_await SendAwaitable{ socket.Handle(), *engine }; ready.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(ready.Error()));

		const int n = ::send(socket.Handle(), reinterpret_cast<const char*>(_data.data()), static_cast<int>(_data.size()), 0);
		if (n < 0)
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[PlainStream/Send]")
			);

		co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(n));
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> PlainStream::Receive(std::span<ne::byte_t> _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "stream is closed" });

		if (auto ready = co_await RecvAwaitable{ socket.Handle(), *engine }; ready.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(ready.Error()));

		const int n = ::recv(socket.Handle(), reinterpret_cast<char*>(_data.data()), static_cast<int>(_data.size()), 0);
		if (n < 0)
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[PlainStream/Receive]")
			);

		co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(n));
	}

	ne::Task<ne::Result<void, ne::OsError>> PlainStream::Shutdown()
	{
		(void)Close();
		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> PlainStream::Close()
	{
		if (!IsOpen()) return ne::Result<void, ne::OsError>::Ok();

		(void)engine->Unwatch(socket.Handle());
		[[maybe_unused]] auto closing = std::move(socket); // fd 는 소멸자에서 닫힘

		return ne::Result<void, ne::OsError>::Ok();
	}

END_NS
