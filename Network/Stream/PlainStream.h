//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include "IStream.h"
#include "Socket/Socket.h"
#include "Engine/IIoEngine.h"

BEGIN_NS(ne::network)
	// 소켓 I/O 모드 선택.
	//   Reactor  : Watch(poll) + recv/send  — 호환성 경로 (epoll 과 유사한 2단계)
	//   Proactor : SubmitRecv/SubmitSend    — 단일 syscall 경로 (IOCP·io_uring 최적)
	enum class IoMode : uint8_t { Reactor, Proactor };

	// 암호화 없는 TCP 스트림.
	// Socket + ne::io::IIoEngine 을 받아 코루틴 기반 비동기 Send/Recv 제공.
	class PlainStream final :public IStream
	{
	public:
		PlainStream(PlainStream&& _other) noexcept;
		PlainStream& operator=(PlainStream&& _other) noexcept;
		virtual ~PlainStream() override = default;

		NEBULA_NON_COPYABLE(PlainStream)

	private:
		explicit PlainStream(Socket&& _socket, ne::io::IIoEngine& _engine,
		                     ne::memory::IAllocator* _allocator, IoMode _mode) noexcept;

	public:
		// _mode = Reactor (기본): 기존 poll+recv/send 2단계 경로 (하위 호환)
		// _mode = Proactor      : SubmitRecv/SubmitSend 단일 경로 (IOCP·io_uring 최적)
		[[nodiscard]] static ne::Result<PlainStream, ne::OsError> Create(
			Socket&& _socket, ne::io::IIoEngine& _engine,
			ne::memory::IAllocator* _allocator = nullptr,
			IoMode _mode = IoMode::Reactor) noexcept;

	private:
		Socket socket;
		ne::io::IIoEngine* engine;
		ne::memory::IAllocator* allocator{ nullptr };
		IoMode ioMode{ IoMode::Reactor };

	public:
		virtual ne::Task<ne::Result<void, ne::OsError>> Handshake() override;
		virtual Task<Result<std::size_t, OsError>> Send(BufferView _data) override;
		virtual Task<Result<std::size_t, OsError>> Sendv(const BufferChain& _chain) override;
		virtual Task<Result<std::size_t, OsError>> Receive(BufferView _data) override;
		virtual ne::Task<ne::Result<void, ne::OsError>> Shutdown() override;
		virtual Result<void, OsError> Close() override;

	public:
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return socket.IsValid(); }
	};

END_NS
