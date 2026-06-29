//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include <span>
#include "IStream.h"
#include "Socket/Socket.h"
#include "IoEngine/IIoEngine.h"

BEGIN_NS(ne::network)
	struct TlsConfig
	{
		bool_t verifyPeer{ true };
		string_t caFile;    // PEM CA bundle (optional)
		string_t certFile;  // PEM client cert (optional)
		string_t keyFile;   // PEM private key (optional)
	};

	class TlsStream final :public IStream
	{
	public:
		NEBULA_NON_COPYABLE(TlsStream)

	private:
#if defined(_WIN32)
		explicit TlsStream(Socket&& _socket, IIoEngine& _engine,
			void* _cred, void* _ctx, void* _buf) noexcept;
#else
		explicit TlsStream(Socket&& _socket, IIoEngine& _engine,
			void* _ctx, void* _ssl) noexcept;
#endif

	public:
		TlsStream(TlsStream&& _other) noexcept;
		TlsStream& operator=(TlsStream&& _other) noexcept;
		~TlsStream() override;

	private:
		Socket socket;
		IIoEngine* engine{};

#if defined(_WIN32)
		void* credHandle{};   // CredHandle*         (SChannel)
		void* ctxHandle{};    // CtxtHandle*         (SChannel)
		void* msgBuffer{};    // TlsMessageBuffer*   (SChannel recv buf)
#else
		void* ctx{};          // SSL_CTX*  (OpenSSL)
		void* ssl{};          // SSL*      (OpenSSL)
#endif

	public:
		// 소켓은 이미 Connect() 되어 있어야 함 (TCP-level).
		// _host : SNI hostname.
		[[nodiscard]] static ne::Task<ne::Result<TlsStream, ne::OsError>> Connect(Socket&& _socket, IIoEngine& _engine, string_view_t _host, const TlsConfig& _config = {});

	public:
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Send(std::span<const byte_t> _data) override;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(std::span<byte_t> _data) override;
		virtual ne::Result<void, ne::OsError> Close() override;

	public:
#if defined(_WIN32)
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return socket.IsValid() && ctxHandle != nullptr; }
#else
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return socket.IsValid() && ssl != nullptr; }
#endif
	};

END_NS
