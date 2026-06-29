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

	struct SshConfig
	{
		string_t username;
		string_t password;       // optional: password auth
		string_t privateKeyFile; // optional: pubkey auth (.pem)
		string_t publicKeyFile;  // optional: pubkey auth (.pem)
		string_t command;        // exec command on channel (empty = shell)
	};

	// SSH exec/shell 채널을 IStream 으로 노출 (libssh2 백엔드).
	// Send  → channel stdin
	// Receive → channel stdout
	class SshStream final : public IStream
	{
	public:
		NEBULA_NON_COPYABLE(SshStream)
		SshStream(SshStream&& _other) noexcept;
		SshStream& operator=(SshStream&& _other) noexcept;
		~SshStream() override;

	private:
		explicit SshStream(Socket&& _socket, IIoEngine& _engine,
		                   void* _session, void* _channel) noexcept;

	public:
		[[nodiscard]] static ne::Task<ne::Result<SshStream, ne::OsError>> Connect(
			Socket&&         _socket,
			IIoEngine&       _engine,
			const SshConfig& _config
		);

	private:
		Socket     socket;
		IIoEngine* engine;
		void*      session{};  // LIBSSH2_SESSION*
		void*      channel{};  // LIBSSH2_CHANNEL*

	public:
		ne::Task<ne::Result<std::size_t, ne::OsError>> Send(std::span<const byte_t> _data) override;
		ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(std::span<byte_t> _data) override;
		ne::Result<void, ne::OsError> Close() override;

	public:
		[[nodiscard]] bool_t IsOpen() const noexcept override
		{
			return socket.IsValid() && channel != nullptr;
		}
	};

END_NS
