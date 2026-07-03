//
// Created by hscloud on 25. 6. 29.
//

#include "PlainStream.h"
#include "Coroutine/Awaitable.h"

#if defined(_WIN32)
#   include <winsock2.h>
#elif defined(IS_POSIX)
#   include <sys/socket.h>
#   include <sys/uio.h>
#endif



BEGIN_NS(ne::network)
	PlainStream::PlainStream(PlainStream&& _other) noexcept
		: socket(std::move(_other.socket))
		, engine(_other.engine)
		, allocator(_other.allocator)
		, ioMode(_other.ioMode) {}

	PlainStream& PlainStream::operator=(PlainStream&& _other) noexcept
	{
		if (this != &_other)
		{
			socket    = std::move(_other.socket);
			engine    = _other.engine;
			allocator = _other.allocator;
			ioMode    = _other.ioMode;
		}
		return *this;
	}

	PlainStream::PlainStream(Socket&& _socket, ne::io::IIoEngine& _engine,
	                         ne::memory::IAllocator* _allocator, const IoMode _mode) noexcept
		: socket(std::move(_socket))
		, engine(&_engine)
		, allocator(_allocator)
		, ioMode(_mode) {}



	ne::Result<PlainStream, ne::OsError> PlainStream::Create(
		Socket&& _socket, ne::io::IIoEngine& _engine,
		ne::memory::IAllocator* _allocator, const IoMode _mode) noexcept
	{
		if (!_socket.IsValid())
			return ne::Result<PlainStream, ne::OsError>::Error(ne::OsError{ 0, "socket is not valid" });

		return ne::Result<PlainStream, ne::OsError>::Ok(
			PlainStream{ std::move(_socket), _engine, _allocator, _mode });
	}



	ne::Task<ne::Result<void, ne::OsError>> PlainStream::Handshake()
	{
		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> PlainStream::Send(BufferView _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "stream is closed" });

		if (ioMode == IoMode::Proactor)
		{
			co_return co_await ne::io::SendSubmitAwaitable{*engine, socket.Handle(), _data.Span().data(), _data.Span().size() };
		}

		// Reactor 경로: poll + send
		if (auto ready = co_await ne::io::SendAwaitable{ socket.Handle(), *engine }; ready.IsError())
			co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(ready.Error()));

		const int bytes = ::send(socket.Handle(), reinterpret_cast<const char*>(_data.Span().data()), static_cast<int>(_data.Span().size()), 0);
		if (bytes < 0)
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[PlainStream/Send]"));

		co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> PlainStream::Sendv(const BufferChain& _chain)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "stream is closed" });

	#if defined(_WIN32)
		std::size_t total = 0;
		for (const auto& segment : _chain.Segments())
		{
			if (auto ready = co_await ne::io::SendAwaitable{ socket.Handle(), *engine }; ready.IsError())
				co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(ready.Error()));

			const int bytes = ::send(socket.Handle(), reinterpret_cast<const char*>(segment.Span().data()), static_cast<int>(segment.Span().size()), 0);
			if (bytes < 0)
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					ne::OsError{ ne::LastOsError() }.Context("[PlainStream/Sendv]"));

			total += static_cast<std::size_t>(bytes);
		}

		co_return ne::Result<std::size_t, ne::OsError>::Ok(total);
	#elif defined(IS_POSIX)
		if (auto ready = co_await ne::io::SendAwaitable{ socket.Handle(), *engine }; ready.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(ready.Error()));

		const auto iov = _chain.AsIovec();
		const ssize_t bytes = ::writev(socket.Handle(), iov.data(), static_cast<int>(iov.size()));
		if (bytes < 0)
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[PlainStream/Sendv]"));

		co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
	#else
		co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "Sendv not supported on this platform" });
	#endif
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> PlainStream::Receive(BufferView _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "stream is closed" });

		if (ioMode == IoMode::Proactor)
		{
			co_return co_await ne::io::ReceiveSubmitAwaitable{*engine, socket.Handle(), _data.ptr, _data.length };
		}

		// Reactor 경로: poll + recv
		if (auto ready = co_await ne::io::ReceiveAwaitable{ socket.Handle(), *engine }; ready.IsError())
			co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(ready.Error()));

		const int bytes = ::recv(socket.Handle(), reinterpret_cast<char*>(_data.ptr), static_cast<int>(_data.length), 0);
		if (bytes < 0)
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[PlainStream/Receive]"));

		co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
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
