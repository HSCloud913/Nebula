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
		bool_t   verifyPeer{ true };
		string_t caFile;    // PEM CA bundle (optional)
		string_t certFile;  // PEM client cert (optional)
		string_t keyFile;   // PEM private key (optional)
	};

	// TLS 스트림 (OpenSSL 백엔드).
	// Connect() 내에서 TLS handshake 까지 완료.
	// 이후 Send/Receive 는 암호화 I/O.
	class TlsStream final : public IStream
	{
	public:
		NEBULA_NON_COPYABLE(TlsStream)
		TlsStream(TlsStream&& _other) noexcept;
		TlsStream& operator=(TlsStream&& _other) noexcept;
		~TlsStream() override;

	private:
		explicit TlsStream(Socket&& _socket, IIoEngine& _engine,
		                   void* _ctx, void* _ssl) noexcept;

	public:
		// 소켓은 이미 Connect() 되어 있어야 함 (TCP-level).
		// _host : SNI hostname.
		[[nodiscard]] static ne::Task<ne::Result<TlsStream, ne::OsError>> Connect(
			Socket&&         _socket,
			IIoEngine&       _engine,
			string_view_t    _host,
			const TlsConfig& _config = {}
		);

	private:
		Socket     socket;
		IIoEngine* engine;
		void*      ctx{};   // SSL_CTX*
		void*      ssl{};   // SSL*

	public:
		ne::Task<ne::Result<std::size_t, ne::OsError>> Send(std::span<const byte_t> _data) override;
		ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(std::span<byte_t> _data) override;
		ne::Result<void, ne::OsError> Close() override;

	public:
		[[nodiscard]] bool_t IsOpen() const noexcept override
		{
			return socket.IsValid() && ssl != nullptr;
		}
	};

END_NS
