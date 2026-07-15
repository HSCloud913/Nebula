//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include <stop_token>
#include "Network/Stream/IStream.h"
#include "Io/Socket/Socket.h"
#include "Io/Context/Context.h"
#include "Io/File/File.h"
#include "Io/Buffer/RegisteredBuffer.h"
#include "Memory/Allocator/IAllocator.h"

BEGIN_NS(ne::network)
	// 암호화 없는 TCP/UDP 바이트 스트림 = ne::io::Socket 위의 얇은 IStream 어댑터(async-only).
	// reactor/proactor 선택과 zero-copy 가용 여부는 Io 레이어(Socket/Engine)가 Capability 로 이미
	// 판단하므로, 이 클래스엔 엔진별 분기가 없다 — Level 1 이상은 엔진을 몰라야 한다(스펙 2.1).
	// TLS/SSH 스트림은 이 객체를 wire transport 로 보유(컴포지션)해 암복호화 계층만 얹는다.
	class PlainStream final :public IStream
	{
	private:
		explicit PlainStream(ne::io::Socket&& _socket, ne::io::Context& _context, ne::memory::IAllocator* _allocator) noexcept
			: socket(std::move(_socket))
			, context(&_context)
			, allocator(_allocator) {}

	public:
		virtual ~PlainStream() override = default;

		PlainStream(PlainStream&& _other) noexcept
			: socket(std::move(_other.socket))
			, context(_other.context)
			, allocator(_other.allocator) {}

		PlainStream& operator=(PlainStream&& _other) noexcept;

		NEBULA_NON_COPYABLE(PlainStream)

	private:
		ne::io::Socket socket;
		ne::io::Context* context;
		ne::memory::IAllocator* allocator{ nullptr };

	public: /* 생성 — server/client 진입점 */
		// client: 호스트 해석(Network::Dns) + 소켓 생성 + 연결까지 접어서 처리한다. 후보(A/AAAA)를
		// 순서대로 시도하며, io::Socket::Connect 가 이미 non-blocking connect + writable 대기 +
		// SO_ERROR 확인을 담당하므로 여기선 후보 페일오버 루프만 있으면 된다.
		[[nodiscard]] static ne::Task<ne::io::IoResult<PlainStream>> Connect(string_view_t _host, uint16_t _port, ne::io::Context& _context, std::stop_token _stopToken = {}, ne::memory::IAllocator* _allocator = nullptr);

		// server: listen 소켓의 io::Socket::Accept() 로 이미 얻은(non-blocking, async-ready) 소켓을 감싼다.
		[[nodiscard]] static ne::io::IoResult<PlainStream> Create(ne::io::Socket&& _socket, ne::io::Context& _context, ne::memory::IAllocator* _allocator = nullptr) noexcept;

	public: /* IStream */
		virtual ne::Task<ne::io::IoResult<void_t>> Handshake(std::stop_token _stopToken = {}) override { co_return ne::io::IoResult<void_t>::Ok(); }
		virtual ne::Task<ne::io::IoResult<std::size_t>> Receive(ne::io::BufferView _data, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Receivev(const ne::io::BufferChain& _chain, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Send(ne::io::BufferView _data, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Sendv(const ne::io::BufferChain& _chain, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<void_t>> Shutdown() override;
		virtual ne::Result<void_t, ne::io::IoError> Close() override;
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return socket.IsValid(); }

	public: /* readiness 대기 — IStream 밖(Plain 전용). wire transport 로 감싸는 상위 스트림(TLS/SSH)이
	        // 자기 자신의 동기 recv/send(EAGAIN/WANT_READ/WANT_WRITE 루프)를 이 completion 엔진 위에서
	        // 구동하기 위해 재사용한다 — io::Socket::WaitReadable/WaitWritable 그대로 위임. */
		[[nodiscard]] ne::Task<ne::io::IoResult<void_t>> WaitReadable(std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<ne::io::IoResult<void_t>> WaitWritable(std::stop_token _stopToken = {});

	public: /* zero-copy 파일 전송 — IStream 밖(Plain 전용) */
		// head/file/tail 조합. head/tail 은 Sendv(BufferChain) 로, file 구간은 io::Socket::SendFile
		// (엔진의 TransmitFile/sendfile zero-copy)로 처리한다 — 플랫폼 분기는 여기 없음.
		[[nodiscard]] ne::Task<ne::io::IoResult<std::size_t>> SendFile(ne::io::file_t _file, ulonglong_t _offset, std::size_t _length, const ne::io::BufferChain& _head = {}, const ne::io::BufferChain& _tail = {}, std::stop_token _stopToken = {});

		// 소켓→파일 수신(SendFile 대칭). [현재 v1] Io 레이어에 splice 류 zero-copy opcode가 없어
		// Receive + io::File::Write 반복(non-zero-copy) 으로 구현한다 — zero-copy 가 필요해지면 Io
		// 엔진에 전용 OpCode 를 추가하는 별도 작업으로 확장한다.
		// _file 은 io::File::Write 로 비동기 기록해야 하므로 raw 핸들이 아니라 이미 열린 io::File 을
		// 참조로 받는다(File 은 Open() 으로만 생성되는 소유 타입이라 raw handle 을 감쌀 방법이 없다).
		// 완료까지 호출자가 _file 을 살려둬야 한다(다른 op 들과 동일 계약).
		[[nodiscard]] ne::Task<ne::io::IoResult<std::size_t>> ReceiveFile(ne::io::File& _file, ulonglong_t _offset, std::size_t _length, std::stop_token _stopToken = {});

	public: /* 등록 버퍼(zero-copy) 송신 — io::Socket::SendZeroCopy 로 위임 */
		// 대칭되는 수신 경로(ReceiveRegistered)는 아직 io::Socket 에 없다 — 필요해지면 Socket 쪽에
		// 먼저 추가한 뒤 여기 반영한다.
		[[nodiscard]] ne::Task<ne::io::IoResult<std::size_t>> SendRegistered(const ne::io::RegisteredBuffer& _buffer, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] ne::io::socket_t Handle() const noexcept { return socket.Handle(); }
		// wire transport 로 감싸는 상위 스트림(TLS/SSH)이 자신의 readiness/버퍼 작업에 재사용한다.
		[[nodiscard]] ne::io::Context& Context() const noexcept { return *context; }
		[[nodiscard]] ne::memory::IAllocator* Allocator() const noexcept { return allocator; }
	};

END_NS
