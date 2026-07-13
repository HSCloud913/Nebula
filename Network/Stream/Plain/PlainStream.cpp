//
// Created by hscloud on 25. 6. 29.
//

#include "Network/Stream/Plain/PlainStream.h"

#include <memory>
#include <utility>
#include "Network/Dns/Dns.h"

#if defined(_WIN32)
#   include <winsock2.h>
#   include <ws2tcpip.h>
#elif defined(IS_POSIX)
#   include <netinet/in.h>
#   include <sys/socket.h>
#endif



BEGIN_NS(ne::network)
	PlainStream::PlainStream(ne::io::Socket&& _socket, ne::io::Context& _context, ne::memory::IAllocator* _allocator) noexcept
		: socket(std::move(_socket))
		, context(&_context)
		, allocator(_allocator) {}

	PlainStream::PlainStream(PlainStream&& _other) noexcept
		: socket(std::move(_other.socket))
		, context(_other.context)
		, allocator(_other.allocator) {}

	PlainStream& PlainStream::operator=(PlainStream&& _other) noexcept
	{
		if (this != &_other)
		{
			socket = std::move(_other.socket);
			context = _other.context;
			allocator = _other.allocator;
		}

		return *this;
	}



	// ─── 팩토리 (server/client) ──────────────────────────────────────────────────

	ne::Task<ne::io::IoResult<PlainStream>> PlainStream::Connect(const string_view_t _host, const uint16_t _port, ne::io::Context& _context, std::stop_token _stopToken, ne::memory::IAllocator* _allocator)
	{
		using R = ne::io::IoResult<PlainStream>;

		auto resolved = co_await dns::Resolve(_host);
		if (resolved.IsError()) co_return R::Error(std::move(resolved.Error()).Context("[PlainStream/Connect]"));

		ne::io::IoError lastError{ ne::io::IoErrorKind::OS_FAILURE, "no candidate address" };

		// 후보(A/AAAA)를 순서대로 시도한다 — io::Socket::Connect 가 이미 non-blocking connect +
		// writable 대기 + SO_ERROR 확인을 담당하므로 여기선 페일오버 루프만 있으면 된다.
		for (const auto& [family, ip] : resolved.Value())
		{
			auto result = ne::io::Socket::Create(_context, family, SOCK_STREAM, IPPROTO_TCP);
			if (result.IsError())
			{
				lastError = std::move(result.Error());
				continue;
			}

			ne::io::Socket sock = std::move(result.Value());
			if (auto connectRes = co_await sock.Connect(ip, _port, _stopToken); connectRes.IsError())
			{
				lastError = std::move(connectRes.Error());
				continue;
			}

			co_return R::Ok(PlainStream{ std::move(sock), _context, _allocator });
		}

		co_return R::Error(std::move(lastError).Context("[PlainStream/Connect]"));
	}

	ne::io::IoResult<PlainStream> PlainStream::Create(ne::io::Socket&& _socket, ne::io::Context& _context, ne::memory::IAllocator* _allocator) noexcept
	{
		using R = ne::io::IoResult<PlainStream>;
		if (!_socket.IsValid()) return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "socket is not valid" }.Context("[PlainStream/Create]"));

		return R::Ok(PlainStream{ std::move(_socket), _context, _allocator });
	}



	// ─── IStream ─────────────────────────────────────────────────────────────────
	ne::Task<ne::io::IoResult<std::size_t>> PlainStream::Receive(const ne::io::BufferView _data, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;
		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "stream is closed" }.Context("[PlainStream/Receive]"));

		co_return co_await socket.Receive(std::span<ne::byte_t>{ _data.ptr, _data.length }, std::move(_stopToken));
	}

	ne::Task<ne::io::IoResult<std::size_t>> PlainStream::Receivev(const ne::io::BufferChain& _chain, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;
		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "stream is closed" }.Context("[PlainStream/Receivev]"));

		co_return co_await socket.Receivev(_chain, std::move(_stopToken));
	}

	ne::Task<ne::io::IoResult<std::size_t>> PlainStream::Send(const ne::io::BufferView _data, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;
		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "stream is closed" }.Context("[PlainStream/Send]"));

		co_return co_await socket.Send(_data.Span(), std::move(_stopToken));
	}

	ne::Task<ne::io::IoResult<std::size_t>> PlainStream::Sendv(const ne::io::BufferChain& _chain, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;
		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "stream is closed" }.Context("[PlainStream/Sendv]"));

		co_return co_await socket.Sendv(_chain, std::move(_stopToken));
	}

	ne::Task<ne::io::IoResult<void_t>> PlainStream::Shutdown()
	{
		using R = ne::io::IoResult<void_t>;
		if (!IsOpen()) co_return R::Ok();

		// send 방향만 닫는 half-close — io::Socket::Shutdown 이 이미 이 의미로 구현돼 있다
		// (양방향 종료는 Close() 책임). TLS close_notify 후 전송종료 등에 쓰인다.
		if (auto result = socket.Shutdown(); result.IsError()) co_return R::Error(std::move(result.Error()).Context("[PlainStream/Shutdown]"));

		co_return R::Ok();
	}

	ne::Result<void_t, ne::io::IoError> PlainStream::Close()
	{
		using R = ne::Result<void_t, ne::io::IoError>;
		if (!IsOpen()) return R::Ok();

		return socket.Close();
	}



	// ─── readiness 대기 ──────────────────────────────────────────────────────────

	ne::Task<ne::io::IoResult<void_t>> PlainStream::WaitReadable(std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<void_t>;
		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "stream is closed" }.Context("[PlainStream/WaitReadable]"));

		co_return co_await socket.WaitReadable(std::move(_stopToken));
	}

	ne::Task<ne::io::IoResult<void_t>> PlainStream::WaitWritable(std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<void_t>;
		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "stream is closed" }.Context("[PlainStream/WaitWritable]"));

		co_return co_await socket.WaitWritable(std::move(_stopToken));
	}



	// ─── zero-copy 파일 전송 ──────────────────────────────────────────────────────

	ne::Task<ne::io::IoResult<std::size_t>> PlainStream::SendFile(const ne::io::file_t _file, const ulonglong_t _offset, const std::size_t _length, const ne::io::BufferChain& _head, const ne::io::BufferChain& _tail, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;
		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "stream is closed" }.Context("[PlainStream/SendFile]"));

		std::size_t total = 0;

		// head — Sendv 재사용(scatter-gather). partial write 는 Suffix() 로 남은 부분만 재구성해 재시도.
		if (!_head.IsEmpty())
		{
			const std::size_t headTotal = _head.TotalSize();
			std::size_t sent = 0;
			while (sent < headTotal)
			{
				auto result = co_await Sendv(sent == 0 ? _head : _head.Suffix(sent), _stopToken);
				if (result.IsError()) co_return R::Error(std::move(result.Error()).Context("[PlainStream/SendFile head]"));
				if (result.Value() == 0) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "peer closed during SendFile head" });
				sent += result.Value();
			}
			total += headTotal;
		}

		// file — io::Socket::SendFile(엔진의 TransmitFile/sendfile zero-copy). 완료가 partial 일 수
		// 있으므로 다 보낼 때까지 반복한다. 0 은 파일이 _length 보다 짧아 EOF 에 닿았다는 뜻.
		std::size_t remaining = _length;
		ulonglong_t offset = _offset;
		while (remaining > 0)
		{
			auto result = co_await socket.SendFile(_file, offset, remaining, _stopToken);
			if (result.IsError()) co_return R::Error(std::move(result.Error()).Context("[PlainStream/SendFile]"));
			if (result.Value() == 0) break;

			total += result.Value();
			offset += result.Value();
			remaining -= result.Value();
		}

		// tail
		if (!_tail.IsEmpty())
		{
			const std::size_t tailTotal = _tail.TotalSize();
			std::size_t sent = 0;
			while (sent < tailTotal)
			{
				auto result = co_await Sendv(sent == 0 ? _tail : _tail.Suffix(sent), _stopToken);
				if (result.IsError()) co_return R::Error(std::move(result.Error()).Context("[PlainStream/SendFile tail]"));
				if (result.Value() == 0) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "peer closed during SendFile tail" });
				sent += result.Value();
			}
			total += tailTotal;
		}

		co_return R::Ok(total);
	}

	ne::Task<ne::io::IoResult<std::size_t>> PlainStream::ReceiveFile(ne::io::File& _file, const ulonglong_t _offset, const std::size_t _length, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;
		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "stream is closed" }.Context("[PlainStream/ReceiveFile]"));

		// v1: recv + io::File::Write 반복(non-zero-copy) — Io 레이어에 splice 류 opcode 가 없어서.
		// chunk 버퍼는 allocator 가 있으면 그걸로, 없으면 프레임 밖 heap(unique_ptr)으로 마련한다.
		constexpr std::size_t chunkSize = 64 * 1024;

		std::unique_ptr<ne::byte_t[]> owned;
		ne::byte_t* chunk = nullptr;
		if (allocator != nullptr) chunk = static_cast<ne::byte_t*>(allocator->Allocate(chunkSize, alignof(ne::byte_t)));
		else
		{
			owned = std::make_unique<ne::byte_t[]>(chunkSize);
			chunk = owned.get();
		}

		struct AllocGuard
		{
			ne::memory::IAllocator* allocator;
			ne::byte_t* ptr;
			std::size_t size;
			~AllocGuard() { if (allocator != nullptr) allocator->Deallocate(ptr, size); }
		} guard{ allocator, chunk, chunkSize };

		std::size_t total = 0;
		ulonglong_t fileOffset = _offset;
		std::size_t remaining = _length;

		while (remaining > 0)
		{
			const std::size_t want = remaining < chunkSize ? remaining : chunkSize;

			auto received = co_await Receive(ne::io::BufferView{ chunk, want }, _stopToken);
			if (received.IsError()) co_return R::Error(std::move(received.Error()).Context("[PlainStream/ReceiveFile receive]"));
			if (received.Value() == 0) break; // 상대 종료(EOF)

			std::size_t writeOffset = 0;
			while (writeOffset < received.Value())
			{
				auto written = co_await _file.Write(std::span<const ne::byte_t>{ chunk + writeOffset, received.Value() - writeOffset }, fileOffset + writeOffset, _stopToken);
				if (written.IsError()) co_return R::Error(std::move(written.Error()).Context("[PlainStream/ReceiveFile write]"));
				if (written.Value() == 0) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "zero-length file write" }.Context("[PlainStream/ReceiveFile write]"));

				writeOffset += written.Value();
			}

			total += received.Value();
			fileOffset += received.Value();
			remaining -= received.Value();
		}

		co_return R::Ok(total);
	}



	// ─── 등록 버퍼(zero-copy) 송신 ────────────────────────────────────────────────

	ne::Task<ne::io::IoResult<std::size_t>> PlainStream::SendRegistered(const ne::io::RegisteredBuffer& _buffer, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;
		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "stream is closed" }.Context("[PlainStream/SendRegistered]"));
		if (!_buffer.IsValid()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::INVALID_BUFFER, "invalid registered buffer" }.Context("[PlainStream/SendRegistered]"));

		const ne::io::BufferView view = _buffer.View();

		// 등록 fast path 우선 시도 — 엔진/소켓이 미지원(UNSUPPORTED)이면 일반 Send 로 투명 폴백한다
		// (호출자는 플랫폼/등록 여부를 몰라도 된다).
		auto result = co_await socket.SendZeroCopy(_buffer.Handle(), view.Span(), _stopToken);
		if (result.IsError() && result.Error().IsUnsupported()) co_return co_await Send(view, std::move(_stopToken));

		co_return result;
	}

END_NS
