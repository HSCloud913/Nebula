//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include "Stream/Plain/PlainStream.h"
#include "Stream/IStream.h"
#include "Engine/IIoEngine.h"

BEGIN_NS(ne::network)
	struct TlsConfig
	{
		bool_t verifyPeer{ true };
		string_t caFile;       // PEM CA bundle (optional)
		string_t certFile;     // PEM cert (OpenSSL) / PFX path (SChannel server)
		string_t keyFile;      // PEM private key (OpenSSL only)
		string_t pfxPassword;  // PFX password (SChannel server only)
	};

	class TlsStream final :public IStream
	{
#if defined(_WIN32)
		explicit TlsStream(PlainStream&& _transport, void* _credHandle, void* _ctxHandle, void* _messageBuffer) noexcept;
#else
		explicit TlsStream(PlainStream&& _transport, void* _ctx, void* _ssl) noexcept;
#endif

	public:
		TlsStream(TlsStream&& _other) noexcept;
		TlsStream& operator=(TlsStream&& _other) noexcept;
		virtual ~TlsStream() override;

		NEBULA_NON_COPYABLE(TlsStream)

	private:
		PlainStream transport; // wire transport (소켓 소유 + fd/수명/engine/allocator 관리)
		string_t sniHost;

#if defined(_WIN32)
		void* credHandle{};    // CredHandle*       (SChannel)
		void* ctxHandle{};     // CtxtHandle*       (SChannel)
		void* messageBuffer{}; // TlsMessageBuffer* (SChannel recv buf)
#else
		void* ctx{};           // SSL_CTX*  (OpenSSL)
		void* ssl{};           // SSL*      (OpenSSL)
#endif

	public:
		// 소켓은 이미 Connect() 되어 있어야 함 (TCP-level).
		// _host : SNI hostname.
		[[nodiscard]] static ne::Task<ne::Result<TlsStream, ne::OsError>> Connect(Socket&& _socket, ne::io::IIoEngine& _engine, string_view_t _host, const TlsConfig& _config = {}, ne::memory::IAllocator* _allocator = nullptr);

		// 소켓은 이미 Accept() 된 클라이언트 소켓이어야 함 (TCP-level).
		// _config.certFile : PFX 경로 (SChannel) 또는 PEM cert 경로 (OpenSSL).
		[[nodiscard]] static ne::Task<ne::Result<TlsStream, ne::OsError>> Accept(Socket&& _socket, ne::io::IIoEngine& _engine, const TlsConfig& _config, ne::memory::IAllocator* _allocator = nullptr);

	public:
		virtual ne::Task<ne::Result<void, ne::OsError>> Handshake() override;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Send(BufferView _data) override;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Sendv(const BufferChain& _chain) override;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(BufferView _data) override;
		virtual ne::Task<ne::Result<void, ne::OsError>> Shutdown() override;
		virtual ne::Result<void, ne::OsError> Close() override;

	public:
#if defined(_WIN32)
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return transport.IsOpen() && ctxHandle != nullptr; }
#else
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return transport.IsOpen() && ssl != nullptr; }
#endif
	};

END_NS
