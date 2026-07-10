//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include "Network/Stream/IStream.h"
#include "Network/Socket/Socket.h"
#include "Io/Engine/IEngine.h"
#include "Io/IoType.h"

BEGIN_NS (ne::network)
// 소켓 I/O 모드 선택.
//   Reactor  : Watch(poll) + recv/send  — 호환성 경로 (epoll 과 유사한 2단계)
//   Proactor : SubmitRecv/SubmitSend    — 단일 syscall 경로 (IOCP·io_uring 최적)
enum class IoMode : uint8_t
{
	Reactor,
	Proactor
};

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
	explicit PlainStream(Socket&& _socket, ne::io::IIoEngine& _engine, ne::memory::IAllocator* _allocator, IoMode _mode) noexcept;

public: /* 생성 — server/client 진입점 */
	// client: 소켓 생성 + 연결까지 접어서 처리한다 (완료 후 non-blocking 으로 둔다).
	[[nodiscard]] static ne::Task<ne::Result<PlainStream, ne::OsError>> Connect(string_view_t _host, uint16_t _port, ne::io::IIoEngine& _engine, IoMode _mode = IoMode::Reactor, ne::memory::IAllocator* _allocator = nullptr);

	// server: Accept 로 얻은 소켓을 감싼다 (non-blocking 으로 둔다).
	[[nodiscard]] static ne::Result<PlainStream, ne::OsError> Accept(Socket&& _accepted, ne::io::IIoEngine& _engine, IoMode _mode = IoMode::Reactor, ne::memory::IAllocator* _allocator = nullptr);

	// 저수준 진입점 — 블로킹 모드를 바꾸지 않는다.
	// _mode = Reactor (기본): poll+recv/send 2단계 경로 (하위 호환)
	// _mode = Proactor      : SubmitRecv/SubmitSend 단일 경로 (IOCP·io_uring 최적)
	[[nodiscard]] static ne::Result<PlainStream, ne::OsError> Create(Socket&& _socket, ne::io::IIoEngine& _engine, ne::memory::IAllocator* _allocator = nullptr, IoMode _mode = IoMode::Reactor) noexcept;

private:
	Socket socket;
	ne::io::IIoEngine* engine;
	ne::memory::IAllocator* allocator{ nullptr };
	IoMode ioMode{ IoMode::Reactor };

public: /* IStream override (non-blocking 소켓 전제) */
	virtual ne::Task<ne::Result<void, ne::OsError>> Handshake() override { co_return ne::Result<void, ne::OsError>::Ok(); }
	virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Send(ne::io::BufferView _data) override;
	virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Sendv(const ne::io::BufferChain& _chain) override;
	virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(ne::io::BufferView _data) override;
	virtual ne::Task<ne::Result<void, ne::OsError>> Shutdown() override;
	virtual ne::Result<void, ne::OsError> Close() override;

public: /* zero-copy scatter-gather — IStream 밖(Plain 전용). 파일 payload 는 CPU 복사 없이 DMA */
	// 반환값: 실제 전송한 총 바이트 (head + file + tail).
	// _head/_tail 은 BufferChain — 여러 세그먼트를 이어붙여 보낼 수 있다(예: HTTP 헤더를
	// 여러 조각으로 구성). 비어 있으면(기본값 {}) 해당 구간은 생략.
	//
	// Linux 는 sendfile(2) 로 zero-copy(Reactor 경로). Windows 는 TransmitFile 로 zero-copy
	// (Proactor 경로, IocpEngine::SubmitTransmitFile) — 다만 TRANSMIT_FILE_BUFFERS 가 head/tail
	// 각각 연속된 버퍼 1개만 지원해서, 2개 이상의 세그먼트가 들어오면 Flatten() 으로 먼저
	// 합친다(이 경우 _allocator 가 반드시 있어야 함).
	[[nodiscard]] ne::Task<ne::Result<std::size_t, ne::OsError>> SendFile(ne::io::file_t _fileFd, std::size_t _offset, std::size_t _size, const ne::io::BufferChain& _head = {}, const ne::io::BufferChain& _tail = {});

	// zero-copy 소켓→파일 수신 (SendFile 의 대칭). 소켓에서 최대 _size 바이트를 받아 _fileFd
	// 의 _offset 위치부터 기록한다. 반환값: 실제 기록한 총 바이트(상대가 _size 전에 닫으면 더 작음).
	//   Linux   : splice(2) (소켓→파이프→파일) 로 유저공간을 거치지 않는 zero-copy.
	//   Windows : 커널 소켓→파일 zero-copy primitive 가 없어 recv+write 폴백(비 zero-copy).
	//             _fileFd 는 동기 핸들이어야 한다(FILE_FLAG_OVERLAPPED 아님).
	[[nodiscard]] ne::Task<ne::Result<std::size_t, ne::OsError>> ReceiveFile(ne::io::file_t _fileFd, std::size_t _offset, std::size_t _size);

public: /* 등록 버퍼 경로 — SendFile/ReceiveFile 과 같은 IStream 밖 Plain 전용 (transport 최적화) */
	// 미리 등록한 버퍼(RegisteredBuffer)로 송수신한다. 엔진이 IoCapability::RegisteredIo 를
	// 제공하면(RIO / io_uring fixed) 검증·lock 을 생략한 fast path 로 가고, 없으면 일반
	// Send/Receive 로 **투명 폴백**한다 — 호출자는 플랫폼/등록 여부를 몰라도 된다(에러 타입도
	// 일반 경로와 동일한 OsError 유지). 등록 자체는 Engine().AsRegisteredBufferProvider() 로
	// 수행하며 그 경로만 IoError 를 쓴다.
	[[nodiscard]] ne::Task<ne::Result<std::size_t, ne::OsError>> SendRegistered(const ne::io::RegisteredBuffer& _buffer);
	[[nodiscard]] ne::Task<ne::Result<std::size_t, ne::OsError>> ReceiveRegistered(ne::io::RegisteredBuffer& _buffer);

public:
	[[nodiscard]] socket_t Handle() const noexcept { return socket.Handle(); }
	[[nodiscard]] IoMode Mode() const noexcept { return ioMode; }
	// wire transport 로 감싸는 상위 스트림(TLS/SSH)이 자신의 readiness/버퍼 작업에 재사용한다.
	[[nodiscard]] ne::io::IIoEngine& Engine() const noexcept { return *engine; }
	[[nodiscard]] ne::memory::IAllocator* Allocator() const noexcept { return allocator; }
	[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return socket.IsValid(); }
};

END_NS
