//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include "Network/Stream/Plain/PlainStream.h"
#include "Network/Stream/IStream.h"
#include "Io/Engine/IEngine.h"

BEGIN_NS (ne::network)

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
	explicit SshStream(PlainStream&& _transport, void* _session, void* _channel) noexcept;

public:
	SshStream(SshStream&& _other) noexcept;
	SshStream& operator=(SshStream&& _other) noexcept;
	virtual ~SshStream() override;

	NEBULA_NON_COPYABLE(SshStream)

private:
	PlainStream transport; // wire transport (소켓 소유 + fd/수명/engine/allocator 관리)
	SshConfig sshConfig;
	void* session{}; // LIBSSH2_SESSION*
	void* channel{}; // LIBSSH2_CHANNEL*

public:
	[[nodiscard]] static ne::Task<ne::Result<SshStream, ne::OsError>> Connect(Socket&& _socket, ne::io::IIoEngine& _engine, const SshConfig& _config, ne::memory::IAllocator* _allocator = nullptr);

public:
	virtual ne::Task<ne::Result<void, ne::OsError>> Handshake() override;
	virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Send(ne::io::BufferView _data) override;
	virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Sendv(const ne::io::BufferChain& _chain) override;
	virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(ne::io::BufferView _data) override;
	virtual ne::Task<ne::Result<void, ne::OsError>> Shutdown() override;
	virtual ne::Result<void, ne::OsError> Close() override;

public:
	[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return transport.IsOpen() && channel != nullptr; }
};

END_NS
