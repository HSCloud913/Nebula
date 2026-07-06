//
// Created by hscloud on 25. 6. 29.
//

#include "PlainStream.h"
#include "Coroutine/Awaitable.h"

#include <cerrno>
#include <utility>

#if defined(_WIN32)
#   include <winsock2.h>
#   include <windows.h>
#   include <mswsock.h>
#   include <ws2tcpip.h>
#   include "Engine/Iocp/IocpEngine.h"
#elif defined(IS_POSIX)
#   include <sys/socket.h>
#   include <sys/uio.h>
#   include <sys/sendfile.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <unistd.h>
#endif



namespace
{
#if defined(IS_POSIX)
	// TCP_CORK 토글 — head/file/tail 을 한 전송 단위로 묶어(코킹) 패킷 분할을 줄인다.
	// 코킹 해제(off) 가 곧 flush 이다.
	void SetCork(const ne::network::socket_t _fd, const bool _on) noexcept
	{
		const int value = _on ? 1 : 0;
		(void)::setsockopt(_fd, IPPROTO_TCP, TCP_CORK, &value, sizeof(value));
	}
#endif
}



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



	// ─── 팩토리 (server/client) ──────────────────────────────────────────────────

	ne::Task<ne::Result<PlainStream, ne::OsError>> PlainStream::Connect(
		const string_view_t _host, const uint16_t _port, ne::io::IIoEngine& _engine,
		const IoMode _mode, ne::memory::IAllocator* _allocator)
	{
		using R = ne::Result<PlainStream, ne::OsError>;

		auto familyRes = co_await Socket::ResolveFamily(_host);
		if (familyRes.IsError())
			co_return R::Error(std::move(familyRes.Error()).Context("[PlainStream/Connect]"));

		auto sockRes = Socket::Create(familyRes.Value(), SOCK_STREAM, IPPROTO_TCP);
		if (sockRes.IsError())
			co_return R::Error(std::move(sockRes.Error()).Context("[PlainStream/Connect]"));

		Socket sock = std::move(sockRes.Value());

		// engine 오버로드: non-blocking connect + writable 대기 + SO_ERROR 확인 — DNS 워커
		// 스레드를 실제 connect 완료까지 붙잡지 않는다. 성공 시 소켓은 이미 non-blocking.
		if (auto r = co_await sock.Connect(_host, _port, _engine); r.IsError())
			co_return R::Error(std::move(r.Error()).Context("[PlainStream/Connect]"));

		co_return R::Ok(PlainStream{ std::move(sock), _engine, _allocator, _mode });
	}

	ne::Result<PlainStream, ne::OsError> PlainStream::Accept(
		Socket&& _accepted, ne::io::IIoEngine& _engine,
		const IoMode _mode, ne::memory::IAllocator* _allocator)
	{
		using R = ne::Result<PlainStream, ne::OsError>;

		if (!_accepted.IsValid())
			return R::Error(ne::OsError{ 0, "socket is not valid" });

		if (auto r = _accepted.SetNonBlocking(true); r.IsError())
			return R::Error(std::move(r.Error()).Context("[PlainStream/Accept]"));

		return R::Ok(PlainStream{ std::move(_accepted), _engine, _allocator, _mode });
	}

	ne::Result<PlainStream, ne::OsError> PlainStream::Create(
		Socket&& _socket, ne::io::IIoEngine& _engine,
		ne::memory::IAllocator* _allocator, const IoMode _mode) noexcept
	{
		if (!_socket.IsValid())
			return ne::Result<PlainStream, ne::OsError>::Error(ne::OsError{ 0, "socket is not valid" });

		return ne::Result<PlainStream, ne::OsError>::Ok(
			PlainStream{ std::move(_socket), _engine, _allocator, _mode });
	}



	// ─── 송수신 (non-blocking 소켓 전제) ─────────────────────────────────────────

	ne::Task<ne::Result<std::size_t, ne::OsError>> PlainStream::Send(ne::io::BufferView _data)
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

	ne::Task<ne::Result<std::size_t, ne::OsError>> PlainStream::Sendv(const ne::io::BufferChain& _chain)
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

			// 이 세그먼트가 partial 하게 전송됐으면 다음 세그먼트로 넘어가지 않고 즉시 멈춘다 —
			// 그렇지 않으면 세그먼트 순서가 뒤섞여 수신측이 잘못된 바이트열을 보게 된다.
			// 호출자가 total(=지금까지의 실제 전송량) 기준으로 BufferChain::Suffix() 재시도.
			if (static_cast<std::size_t>(bytes) < segment.length) break;
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

	ne::Task<ne::Result<std::size_t, ne::OsError>> PlainStream::Receive(ne::io::BufferView _data)
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



	// ─── 수명 ────────────────────────────────────────────────────────────────────

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



	// ─── zero-copy scatter-gather 파일 전송 ──────────────────────────────────────

	ne::Task<ne::Result<std::size_t, ne::OsError>> PlainStream::SendFile(
		const ne::io::file_t _fileFd, const std::size_t _offset, const std::size_t _size,
		const ne::io::BufferChain& _head, const ne::io::BufferChain& _tail)
	{
		using R = ne::Result<std::size_t, ne::OsError>;
		if (!IsOpen()) co_return R::Error(ne::OsError{ 0, "stream is closed" });

	#if defined(IS_POSIX)
		const socket_t fd = socket.Handle();
		const bool_t hasHead = !_head.IsEmpty();
		const bool_t hasTail = !_tail.IsEmpty();
		const bool_t cork    = hasHead || hasTail;

		// 코킹 해제(flush)를 모든 종료 경로에서 보장 — 코루틴 프레임 소멸 시 실행.
		struct CorkGuard { socket_t fd; bool_t active; ~CorkGuard() noexcept { if (active) SetCork(fd, false); } } guard{ fd, cork };
		if (cork) SetCork(fd, true);

		std::size_t total = 0;

		// head — Sendv 경로 재사용(scatter-gather, reactor/proactor 모두 반영). Sendv 는 partial
		// write 를 그대로 반환하므로 다 보낼 때까지 Suffix() 로 남은 부분만 재구성해 재시도한다.
		if (hasHead)
		{
			const std::size_t headTotal = _head.TotalSize();
			std::size_t sent = 0;
			while (sent < headTotal)
			{
				auto r = co_await Sendv(sent == 0 ? _head : _head.Suffix(sent));
				if (r.IsError()) co_return R::Error(std::move(r.Error()).Context("[PlainStream/SendFile head]"));
				if (r.Value() == 0) co_return R::Error(ne::OsError{ 0, "peer closed during SendFile head" });
				sent += r.Value();
			}
			total += headTotal;
		}

		// file — sendfile(2) zero-copy. EAGAIN 이면 writable 대기 후 재시도.
		off_t offset          = static_cast<off_t>(_offset);
		std::size_t remaining = _size;
		while (remaining > 0)
		{
			const ssize_t bytes = ::sendfile(fd, _fileFd, &offset, remaining);
			if (bytes > 0) { total += static_cast<std::size_t>(bytes); remaining -= static_cast<std::size_t>(bytes); continue; }
			if (bytes == 0) break; // 파일이 _size 보다 짧음(EOF)

			const ne::ulong_t error = ne::LastOsError();
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				if (auto ready = co_await ne::io::SendAwaitable{ fd, *engine }; ready.IsError())
					co_return R::Error(std::move(ready.Error()));
				continue;
			}

			co_return R::Error(ne::OsError{ error }.Context("[PlainStream/SendFile]"));
		}

		// tail
		if (hasTail)
		{
			const std::size_t tailTotal = _tail.TotalSize();
			std::size_t sent = 0;
			while (sent < tailTotal)
			{
				auto r = co_await Sendv(sent == 0 ? _tail : _tail.Suffix(sent));
				if (r.IsError()) co_return R::Error(std::move(r.Error()).Context("[PlainStream/SendFile tail]"));
				if (r.Value() == 0) co_return R::Error(ne::OsError{ 0, "peer closed during SendFile tail" });
				sent += r.Value();
			}
			total += tailTotal;
		}

		co_return R::Ok(total);

	#elif defined(_WIN32)
		// Proactor + zero-copy: TransmitFile 이 파일 내용을 커널 안에서 소켓으로 직접 전송한다.
		auto& iocpEngine = static_cast<ne::io::IocpEngine&>(*engine);

		const bool_t hasHead = !_head.IsEmpty();
		const bool_t hasTail = !_tail.IsEmpty();

		// TRANSMIT_FILE_BUFFERS 는 head/tail 각각 연속된 버퍼 1개만 지원한다 — 세그먼트가
		// 여럿이면 먼저 Flatten() 으로 합친다(TlsStream/SshStream::Sendv 와 동일 패턴).
		ne::io::BufferView headView{};
		bool_t ownsHead = false;
		if (hasHead)
		{
			if (_head.Segments().size() == 1)
			{
				headView = _head.Segments().front();
			}
			else
			{
				if (!allocator) co_return R::Error(ne::OsError{ 0, "SendFile: multi-segment head requires an allocator on Windows" });
				headView = _head.Flatten(*allocator);
				if (!headView.IsValid()) co_return R::Error(ne::OsError{ 0, "SendFile: head Flatten failed" });
				ownsHead = true;
			}
		}

		ne::io::BufferView tailView{};
		bool_t ownsTail = false;
		if (hasTail)
		{
			if (_tail.Segments().size() == 1)
			{
				tailView = _tail.Segments().front();
			}
			else
			{
				if (!allocator)
				{
					if (ownsHead) headView.owner->Release();
					co_return R::Error(ne::OsError{ 0, "SendFile: multi-segment tail requires an allocator on Windows" });
				}
				tailView = _tail.Flatten(*allocator);
				if (!tailView.IsValid())
				{
					if (ownsHead) headView.owner->Release();
					co_return R::Error(ne::OsError{ 0, "SendFile: tail Flatten failed" });
				}
				ownsTail = true;
			}
		}

		auto result = co_await ne::io::TransmitFileSubmitAwaitable{
			iocpEngine, socket.Handle(), _fileFd, _offset, _size,
			hasHead ? std::span<ne::byte_t>(headView.ptr, headView.length) : std::span<ne::byte_t>{},
			hasTail ? std::span<ne::byte_t>(tailView.ptr, tailView.length) : std::span<ne::byte_t>{}
		};

		if (ownsHead) headView.owner->Release();
		if (ownsTail) tailView.owner->Release();

		if (result.IsError()) co_return R::Error(std::move(result.Error()).Context("[PlainStream/SendFile]"));

		co_return R::Ok(result.Value());
	#else
		(void)_fileFd; (void)_offset; (void)_size; (void)_head; (void)_tail;
		co_return R::Error(ne::OsError{ 0, "SendFile not supported on this platform" });
	#endif
	}

END_NS
