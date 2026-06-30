//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <functional>
#include <optional>
#include <span>
#include <vector>
#include "SftpBase.h"
#include "Stream/SshStream.h"  // for ne::network::SshConfig
#include "Socket/Socket.h"
#include "Coroutine/Task.h"
#include "Result.h"
#include "Error.h"

BEGIN_NS(ne::network::sftp)
	// Async SFTP client backed by libssh2.
	// Manages its own TCP socket + SSH session + SFTP subsystem.
	// SshConfig is reused from ne::network::SshStream for configuration parity.
	//
	// NOTE: libssh2 operations run in blocking mode on the calling coroutine.
	//       This is correct for single-threaded event loops where SFTP I/O is
	//       offloaded; non-blocking integration with IIoEngine is a future work item.
	class Client
	{
	public:
		Client() noexcept = default;
		Client(Client&&) noexcept;
		Client& operator=(Client&&) noexcept;
		~Client();
		NEBULA_NON_COPYABLE(Client)

	public:
		[[nodiscard]] static ne::Task<ne::Result<Client, ne::OsError>> Connect(string_view_t _host, uint16_t _port, const ne::network::SshConfig& _config);

	private:
		using SinkFn_t = std::function<void(std::span<const ne::byte_t>)>;

		std::optional<ne::network::Socket> socket;
		void* session{ nullptr };  // LIBSSH2_SESSION*
		void* sftpHandle{ nullptr };  // LIBSSH2_SFTP*

	public:
		[[nodiscard]] ne::Task<ne::Result<std::vector<Entry>, ne::OsError>> List(string_view_t _path);
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> Get(string_view_t _remote, const SinkFn_t& _sink);
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> Put(string_view_t _remote, std::span<const ne::byte_t> _data);
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> Mkdir(string_view_t _path);
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> Rmdir(string_view_t _path);
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> Remove(string_view_t _path);
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> Rename(string_view_t _from, string_view_t _to);
		[[nodiscard]] ne::Task<ne::Result<Entry, ne::OsError>> Stat(string_view_t _path);

	private:
		void Disconnect() noexcept;
	};

END_NS
