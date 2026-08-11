//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include <stop_token>
#include <vector>
#include "Network/Stream/PlainStream.h"
#include "Network/Stream/IStream.h"
#include "Network/Stream/Tls/TlsConfig.h"
#include "Io/Socket.h"
#include "Memory/Allocator/IAllocator.h"

namespace ne::network
{
	/**
	 * @class TlsStream
	 * @brief TLS 1.2/1.3 스트림입니다 — Windows는 Schannel(SSPI), POSIX는 OpenSSL을 사용합니다.
	 *
	 * PlainStream을 wire transport로 컴포지션해 암복호화 계층만 얹습니다(PlainStream.h의 설계
	 * 원칙 그대로). 백엔드가 요구하는 것은 "레코드 바이트를 보내고/받아달라"는 것뿐이므로, wire I/O는
	 * Internal/Transfer.h의 SendAll/RecvSome(= PlainStream의 completion 기반 Send/Receive)만
	 * 사용합니다.
	 *
	 * @note 과거에는 raw 소켓 핸들 + readiness 대기(WaitReadable/WaitWritable) + 동기 ::send/::recv로
	 * 구동했습니다. 그 구조는 소켓이 블로킹이어야 성립하는데, 블로킹 소켓은 리액터 엔진의 이벤트 루프를
	 * 정지시킵니다. completion 기반으로 바꾸면서 그 결합이 사라졌습니다.
	 */
	class TlsStream final :public IStream
	{
#if defined(_WIN32)
		explicit TlsStream(PlainStream&& _transport, void* _credHandle, void* _ctxHandle, void* _messageBuffer, ne::memory::IAllocator* _allocator) noexcept
			: transport(std::move(_transport))
			, allocator(_allocator)
			, credHandle(_credHandle)
			, ctxHandle(_ctxHandle)
			, messageBuffer(_messageBuffer) {}
#elif defined(NEBULA_WITH_OPENSSL)
		explicit TlsStream(PlainStream&& _transport, void* _ctx, void* _ssl, ne::memory::IAllocator* _allocator) noexcept
			: transport(std::move(_transport))
			, allocator(_allocator)
			, ctx(_ctx)
			, ssl(_ssl) {}
#else
		explicit TlsStream(PlainStream&& _transport, ne::memory::IAllocator* _allocator) noexcept
			: transport(std::move(_transport))
			, allocator(_allocator) {}
#endif

	public:
		virtual ~TlsStream() override;

		TlsStream(TlsStream&& _other) noexcept
			: transport(std::move(_other.transport))
			, sniHost(std::move(_other.sniHost))
			, alpnCandidates(std::move(_other.alpnCandidates))
			, negotiatedProtocol(std::move(_other.negotiatedProtocol))
			, allocator(_other.allocator)
#if defined(_WIN32)
			, credHandle(std::exchange(_other.credHandle, nullptr))
			, ctxHandle(std::exchange(_other.ctxHandle, nullptr))
			, messageBuffer(std::exchange(_other.messageBuffer, nullptr)) {}
#elif defined(NEBULA_WITH_OPENSSL)
			, ctx(std::exchange(_other.ctx, nullptr))
			, ssl(std::exchange(_other.ssl, nullptr)) {}
#else
			{} // 백엔드 없음(POSIX + OpenSSL 미발견) — 멤버가 없으므로 본문만 필요
#endif
		TlsStream& operator=(TlsStream&& _other) noexcept;


		NEBULA_NON_COPYABLE(TlsStream)

	private:
		PlainStream transport; // wire transport(소켓 소유 + fd/수명/context/allocator 관리)
		string_t sniHost;
		std::vector<string_t> alpnCandidates; // TlsConfig::alpnProtocols 복사본 — Handshake()/Accept() 가 소비
		string_t negotiatedProtocol;          // ALPN 결과, 없으면 빈 문자열
		ne::memory::IAllocator* allocator{ nullptr };

#if defined(_WIN32)
		void* credHandle{};    // CredHandle*       (SChannel)
		void* ctxHandle{};     // CtxtHandle*       (SChannel)
		void* messageBuffer{}; // TlsMessageBuffer* (SChannel recv buf)
#elif defined(NEBULA_WITH_OPENSSL)
		void* ctx{}; // SSL_CTX*  (OpenSSL)
		void* ssl{}; // SSL*      (OpenSSL)
#endif

	public:
		/** @brief TCP 연결이 이미 끝난 소켓 위에서 TLS 클라이언트 핸드셰이크를 수행합니다. _host는 SNI hostname입니다. */
		[[nodiscard]] static ne::Task<ne::io::IoResult<TlsStream>> Connect(ne::io::Socket&& _socket, ne::io::Context& _context, string_view_t _host, const TlsConfig& _config = {}, std::stop_token _stopToken = {}, ne::memory::IAllocator* _allocator = nullptr);

		/**
		 * @brief 이미 Accept() 된 TCP 클라이언트 소켓 위에서 TLS 서버 핸드셰이크를 수행합니다.
		 * @note _config.certFile 은 PFX 경로(SChannel) 또는 PEM cert 경로(OpenSSL)를 의미합니다.
		 */
		[[nodiscard]] static ne::Task<ne::io::IoResult<TlsStream>> Accept(ne::io::Socket&& _socket, ne::io::Context& _context, const TlsConfig& _config, std::stop_token _stopToken = {}, ne::memory::IAllocator* _allocator = nullptr);

	public: /* IStream */
		virtual ne::Task<ne::io::IoResult<void_t>> Handshake(std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Receive(ne::memory::BufferView _data, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Receivev(const ne::memory::BufferChain& _chain, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Send(ne::memory::BufferView _data, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Sendv(const ne::memory::BufferChain& _chain, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<void_t>> Shutdown() override;
		virtual ne::io::IoResult<void_t> Close() override;

#if defined(_WIN32)
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return transport.IsOpen() && ctxHandle != nullptr; }
#elif defined(NEBULA_WITH_OPENSSL)
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return transport.IsOpen() && ssl != nullptr; }
#else
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return false; }
#endif

	public:
		/** @brief 핸드셰이크 완료 후 ALPN 협상 결과(없으면 빈 문자열)입니다. Connect()/Accept() 성공 이후에만 의미 있습니다. */
		[[nodiscard]] string_view_t NegotiatedProtocol() const noexcept { return negotiatedProtocol; }
	};
}
