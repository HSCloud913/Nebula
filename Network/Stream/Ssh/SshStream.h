//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include "Socket/Socket.h"
#include "Stream/IStream.h"
#include "Engine/IIoEngine.h"

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
	class SshStream final :public IStream
	{
		explicit SshStream(Socket&& _socket, ne::io::IIoEngine& _engine, void* _session, void* _channel, ne::memory::IAllocator* _allocator = nullptr) noexcept;

	public:
		SshStream(SshStream&& _other) noexcept;
		SshStream& operator=(SshStream&& _other) noexcept;
		~SshStream() override;
		NEBULA_NON_COPYABLE(SshStream)

	private:
		Socket socket;
		ne::io::IIoEngine* engine;
		SshConfig sshConfig;
		ne::memory::IAllocator* allocator{};
		void* session{};  // LIBSSH2_SESSION*
		void* channel{};  // LIBSSH2_CHANNEL*

	public:
		[[nodiscard]] static ne::Task<ne::Result<SshStream, ne::OsError>> Connect(Socket&& _socket, ne::io::IIoEngine& _engine, const SshConfig& _config, ne::memory::IAllocator* _allocator = nullptr);

	public:
		virtual ne::Task<ne::Result<void, ne::OsError>> Handshake() override;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Send(BufferView _data) override;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Sendv(const BufferChain& _chain) override;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(BufferView _data) override;
		virtual ne::Task<ne::Result<void, ne::OsError>> Shutdown() override;
		virtual ne::Result<void, ne::OsError> Close() override;

	public:
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return socket.IsValid() && channel != nullptr; }
	};

END_NS
