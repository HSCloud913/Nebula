//
// Created by hscloud on 25. 6. 29.
//
// TLS 1.2/1.3 스트림 — Windows 는 Schannel(SSPI), POSIX 는 OpenSSL. PlainStream 을 wire
// transport 로 컴포지션해 암복호화 계층만 얹는다(PlainStream.h 의 설계 원칙 그대로). Schannel/OpenSSL
// 둘 다 "동기 send/recv + EAGAIN/WANT_READ/WANT_WRITE" 스타일 라이브러리라, PlainStream 의 completion
// 기반 Send/Receive 를 쓰지 않고 raw socket 핸들 + PlainStream::WaitReadable/WaitWritable(readiness
// 프리미티브)로 직접 구동한다 — libssh2 를 얹는 SshStream 과 동일한 패턴.

#pragma once
#include <cstddef>
#include <stop_token>
#include <vector>
#include "Network/Stream/Plain/PlainStream.h"
#include "Network/Stream/IStream.h"
#include "Io/Context/Context.h"
#include "Io/Socket/Socket.h"
#include "Memory/Allocator/IAllocator.h"

BEGIN_NS(ne::network)
	struct TlsConfig
	{
		bool_t verifyPeer{ true };
		string_t caFile;      // PEM CA bundle (optional)
		string_t certFile;    // PEM cert (OpenSSL) / PFX path (SChannel server)
		string_t keyFile;     // PEM private key (OpenSSL only)
		string_t pfxPassword; // PFX password (SChannel server only)

		// ALPN 협상 후보(우선순위 순서, 예: {"h2","http/1.1"}). 비어있으면 ALPN 확장 자체를 안 보낸다.
		// 서버(Accept) 쪽에서는 클라이언트 제안 중 이 목록의 우선순위에 맞는 걸 고른다.
		std::vector<string_t> alpnProtocols;
	};

	class TlsStream final :public IStream
	{
#if defined(_WIN32)
		explicit TlsStream(PlainStream&& _transport, void* _credHandle, void* _ctxHandle, void* _messageBuffer, ne::memory::IAllocator* _allocator) noexcept;
#elif defined(NEBULA_WITH_OPENSSL)
		explicit TlsStream(PlainStream&& _transport, void* _ctx, void* _ssl, ne::memory::IAllocator* _allocator) noexcept;
#else
		explicit TlsStream(PlainStream&& _transport, ne::memory::IAllocator* _allocator) noexcept;
#endif

	public:
		TlsStream(TlsStream&& _other) noexcept;
		TlsStream& operator=(TlsStream&& _other) noexcept;
		virtual ~TlsStream() override;

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
		// 소켓은 이미 Connect() 되어 있어야 함(TCP-level). _host: SNI hostname.
		[[nodiscard]] static ne::Task<ne::io::IoResult<TlsStream>> Connect(ne::io::Socket&& _socket, ne::io::Context& _context, string_view_t _host, const TlsConfig& _config = {}, std::stop_token _stopToken = {}, ne::memory::IAllocator* _allocator = nullptr);

		// 소켓은 이미 Accept() 된 클라이언트 소켓이어야 함(TCP-level).
		// _config.certFile: PFX 경로(SChannel) 또는 PEM cert 경로(OpenSSL).
		[[nodiscard]] static ne::Task<ne::io::IoResult<TlsStream>> Accept(ne::io::Socket&& _socket, ne::io::Context& _context, const TlsConfig& _config, std::stop_token _stopToken = {}, ne::memory::IAllocator* _allocator = nullptr);

	public: /* IStream */
		virtual ne::Task<ne::io::IoResult<void_t>> Handshake(std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Receive(ne::io::BufferView _data, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Receivev(const ne::io::BufferChain& _chain, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Send(ne::io::BufferView _data, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<std::size_t>> Sendv(const ne::io::BufferChain& _chain, std::stop_token _stopToken = {}) override;
		virtual ne::Task<ne::io::IoResult<void_t>> Shutdown() override;
		virtual ne::Result<void_t, ne::io::IoError> Close() override;

#if defined(_WIN32)
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return transport.IsOpen() && ctxHandle != nullptr; }
#elif defined(NEBULA_WITH_OPENSSL)
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return transport.IsOpen() && ssl != nullptr; }
#else
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return false; }
#endif

	public:
		// 핸드셰이크 완료 후 ALPN 협상 결과(없으면 빈 문자열). Handshake()/Connect()/Accept() 성공 이후에만 의미 있다.
		[[nodiscard]] string_view_t NegotiatedProtocol() const noexcept { return negotiatedProtocol; }
	};

END_NS
