//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include <stop_token>
#include "Network/Stream/IStream.h"
#include "Io/Socket.h"
#include "Io/Context.h"
#include "Io/File.h"
#include "Io/Buffer/RegisteredBuffer.h"
#include "Memory/Allocator/IAllocator.h"

namespace ne::network
{
	/**
	 * @class PlainStream
	 * @brief 암호화 없는 TCP/UDP 바이트 스트림입니다 — ne::io::Socket 위의 얇은 IStream 어댑터(async-only)입니다.
	 *
	 * reactor/proactor 선택과 zero-copy 가용 여부는 Io 레이어(Socket/Engine)가 Capability로 이미
	 * 판단하므로, 이 클래스엔 엔진별 분기가 없습니다 — Level 1 이상은 엔진을 몰라야 합니다(스펙 2.1).
	 *
	 * @note TLS/SSH 스트림은 이 객체를 wire transport로 보유(컴포지션)해 암복호화 계층만 얹습니다.
	 */
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
		/**
		 * @brief 클라이언트 진입점입니다. 호스트 해석(Network::Dns) + 소켓 생성 + 연결까지 한번에 처리합니다.
		 *
		 * 후보(A/AAAA)를 순서대로 시도하며, io::Socket::Connect가 이미 non-blocking connect +
		 * writable 대기 + SO_ERROR 확인을 담당하므로 여기선 후보 페일오버 루프만 있으면 됩니다.
		 */
		[[nodiscard]] static ne::Task<ne::io::IoResult<PlainStream>> Connect(string_view_t _host, uint16_t _port, ne::io::Context& _context, std::stop_token _stopToken = {}, ne::memory::IAllocator* _allocator = nullptr);

		/** @brief 서버 진입점입니다. listen 소켓의 io::Socket::Accept()로 이미 얻은(non-blocking, async-ready) 소켓을 감쌉니다. */
		[[nodiscard]] static ne::io::IoResult<PlainStream> Create(ne::io::Socket&& _socket, ne::io::Context& _context, ne::memory::IAllocator* _allocator = nullptr) noexcept;

	public: /* IStream */
		/** @brief 암호화 계층이 없으므로 아무 것도 하지 않고 즉시 성공을 반환합니다. */
		virtual ne::Task<ne::io::IoResult<void_t>> Handshake(std::stop_token _stopToken = {}) override { co_return ne::io::IoResult<void_t>::Ok(); }
		virtual ne::Task<ne::io::IoResult<std::size_t>> Receive(ne::memory::BufferView _data, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Receivev(const ne::memory::BufferChain& _chain, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Send(ne::memory::BufferView _data, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Sendv(const ne::memory::BufferChain& _chain, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<void_t>> Shutdown() override;
		virtual ne::io::IoResult<void_t> Close() override;
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return socket.IsValid(); }

	public:
		/**
		 * @brief 읽기 준비 상태를 비동기로 기다립니다. IStream 계약 밖의 Plain 전용 API입니다.
		 *
		 * wire transport로 이 스트림을 감싸는 상위 스트림(TLS/SSH)이, 자신의 동기 recv/send
		 * (EAGAIN/WANT_READ/WANT_WRITE 루프)를 이 completion 엔진 위에서 구동하기 위해 재사용합니다.
		 * io::Socket::WaitReadable 로 그대로 위임됩니다.
		 */
		[[nodiscard]] ne::Task<ne::io::IoResult<void_t>> WaitReadable(std::stop_token _stopToken = {});
		/** @brief WaitReadable() 의 쓰기 준비 버전입니다(io::Socket::WaitWritable 로 위임). */
		[[nodiscard]] ne::Task<ne::io::IoResult<void_t>> WaitWritable(std::stop_token _stopToken = {});

	public: /* zero-copy 파일 전송 */
		/**
		 * @brief head/file/tail 을 순서대로 전송합니다. file 구간만 zero-copy 로 처리됩니다.
		 *
		 * head/tail 은 Sendv(BufferChain)로, file 구간은 io::Socket::SendFile(엔진의
		 * TransmitFile/sendfile zero-copy)로 처리하며, 플랫폼별 분기는 이 함수에 없습니다.
		 */
		[[nodiscard]] ne::Task<ne::io::IoResult<std::size_t>> SendFile(ne::io::file_t _file, ulonglong_t _offset, std::size_t _length, const ne::memory::BufferChain& _head = {}, const ne::memory::BufferChain& _tail = {}, std::stop_token _stopToken = {});

		/**
		 * @brief 소켓에서 받은 데이터를 파일에 기록합니다(SendFile의 대칭 연산).
		 *
		 * @note [v1] Io 레이어에 splice 류 zero-copy RequestKind가 없어 Receive + io::File::Write
		 * 반복(non-zero-copy)으로 구현되어 있습니다. _file은 io::File::Write로 비동기 기록해야
		 * 하므로 raw 핸들이 아니라 이미 열린 io::File을 참조로 받으며, 완료까지 호출자가 살려둬야
		 * 합니다(다른 op 들과 동일한 계약).
		 */
		[[nodiscard]] ne::Task<ne::io::IoResult<std::size_t>> ReceiveFile(ne::io::File& _file, ulonglong_t _offset, std::size_t _length, std::stop_token _stopToken = {});

	public: /* 등록 버퍼(zero-copy) 송신 */
		/**
		 * @brief 사전 등록된 버퍼를 zero-copy로 전송합니다.
		 * @note 대칭되는 수신 경로(ReceiveRegistered)는 아직 io::Socket에 없습니다 — 필요해지면
		 * Socket 쪽에 먼저 추가한 뒤 여기 반영합니다.
		 */
		[[nodiscard]] ne::Task<ne::io::IoResult<std::size_t>> SendRegistered(const ne::io::RegisteredBuffer& _buffer, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] ne::io::socket_t Handle() const noexcept { return socket.Handle(); }
		[[nodiscard]] ne::io::Context& Context() const noexcept { return *context; }
		[[nodiscard]] ne::memory::IAllocator* Allocator() const noexcept { return allocator; }
	};
}
