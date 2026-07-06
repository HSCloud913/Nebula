//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include "../IStream.h"
#include "Socket/Socket.h"
#include "Engine/IIoEngine.h"
#include "IoType.h"

BEGIN_NS(ne::network)
	// 소켓 I/O 모드 선택.
	//   Reactor  : Watch(poll) + recv/send  — 호환성 경로 (epoll 과 유사한 2단계)
	//   Proactor : SubmitRecv/SubmitSend    — 단일 syscall 경로 (IOCP·io_uring 최적)
	enum class IoMode : uint8_t { Reactor, Proactor };

	// 암호화 없는 TCP 바이트 스트림 = 소켓 전송 코어 (async-only).
	// Socket + ne::io::IIoEngine 위에서 코루틴 기반 송수신과 zero-copy 파일 전송을 제공한다.
	// TLS/SSH 스트림은 이 객체를 wire transport 로 보유(컴포지션)해 암복호화 계층만 얹는다.
	//
	// 축:
	//   server/client    : Connect(client) / Accept(server) 팩토리 — 이후 송수신은 대칭
	//   reactor/proactor : IoMode 로 async 경로 내부 선택
	//   zero-copy S-G    : SendFile (head/tail scatter-gather, 파일 payload 는 DMA)
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

	public: /* 생성 — server/client 진입점 */
		// client: 소켓 생성 + 연결까지 접어서 처리한다 (완료 후 non-blocking 으로 둔다).
		[[nodiscard]] static ne::Task<ne::Result<PlainStream, ne::OsError>> Connect(
			string_view_t _host, uint16_t _port, ne::io::IIoEngine& _engine,
			IoMode _mode = IoMode::Reactor, ne::memory::IAllocator* _allocator = nullptr);

		// server: Accept 로 얻은 소켓을 감싼다 (non-blocking 으로 둔다).
		[[nodiscard]] static ne::Result<PlainStream, ne::OsError> Accept(
			Socket&& _accepted, ne::io::IIoEngine& _engine,
			IoMode _mode = IoMode::Reactor, ne::memory::IAllocator* _allocator = nullptr);

		// 저수준 진입점 — 블로킹 모드를 바꾸지 않는다.
		// _mode = Reactor (기본): poll+recv/send 2단계 경로 (하위 호환)
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

	public: /* IStream override (non-blocking 소켓 전제) */
		virtual ne::Task<ne::Result<void, ne::OsError>> Handshake() override { co_return ne::Result<void, ne::OsError>::Ok(); }
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Send(BufferView _data) override;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Sendv(const BufferChain& _chain) override;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(BufferView _data) override;
		virtual ne::Task<ne::Result<void, ne::OsError>> Shutdown() override;
		virtual ne::Result<void, ne::OsError> Close() override;

	public: /* zero-copy scatter-gather — IStream 밖(Plain 전용). 파일 payload 는 CPU 복사 없이 DMA */
		// 반환값: 실제 전송한 총 바이트 (head + file + tail).
		// _head/_tail 은 owner 없는 BufferView 도 허용(ptr/length 만 유효하면 됨). 없으면 기본값 {}.
		//
		// NOTE: Windows 는 TransmitFile 이 overlapped(IOCP) 전용이라 엔진 SubmitSendFile
		//       도입 전까지 미지원(에러 반환). Linux 는 sendfile(2) 로 완전 동작.
		[[nodiscard]] ne::Task<ne::Result<std::size_t, ne::OsError>> SendFile(
			ne::io::file_t _fileFd, std::size_t _offset, std::size_t _size,
			BufferView _head = {}, BufferView _tail = {});

	public:
		[[nodiscard]] socket_t Handle() const noexcept { return socket.Handle(); }
		[[nodiscard]] IoMode Mode() const noexcept { return ioMode; }
		// wire transport 로 감싸는 상위 스트림(TLS/SSH)이 자신의 readiness/버퍼 작업에 재사용한다.
		[[nodiscard]] ne::io::IIoEngine& Engine() const noexcept { return *engine; }
		[[nodiscard]] ne::memory::IAllocator* Allocator() const noexcept { return allocator; }
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return socket.IsValid(); }
	};

END_NS
