//
// Created by hscloud on 26. 6. 30.
//

#include "Network/Protocol/Sftp/Client.h"
#include <format>

#if defined(NEBULA_WITH_LIBSSH2)
#   include <libssh2.h>
#   include <libssh2_sftp.h>
#endif

#if defined(_WIN32)
#   include <winsock2.h>
#   include <ws2tcpip.h>
#elif defined(IS_POSIX)
#   include <sys/select.h>
#   include <unistd.h>
#endif

BEGIN_NS(ne::network::sftp)
	// ── move / destructor ─────────────────────────────────────────────────────────

	Client::Client(Client&& _other) noexcept
		: socket(std::move(_other.socket))
		, session(std::exchange(_other.session, static_cast<void*>(nullptr)))
		, sftpHandle(std::exchange(_other.sftpHandle, static_cast<void*>(nullptr))) {}

	Client& Client::operator=(Client&& _other) noexcept
	{
		if (this != &_other)
		{
			Disconnect();
			socket = std::move(_other.socket);
			session = std::exchange(_other.session, static_cast<void*>(nullptr));
			sftpHandle = std::exchange(_other.sftpHandle, static_cast<void*>(nullptr));
		}
		return *this;
	}

	Client::~Client() { Disconnect(); }

	void Client::Disconnect() noexcept
	{
#if defined(NEBULA_WITH_LIBSSH2)
		if (sftpHandle)
		{
			::libssh2_sftp_shutdown(static_cast<LIBSSH2_SFTP*>(sftpHandle));
			sftpHandle = static_cast<void*>(nullptr);
		}
		if (session)
		{
			::libssh2_session_disconnect(static_cast<LIBSSH2_SESSION*>(session), "Bye");
			::libssh2_session_free(static_cast<LIBSSH2_SESSION*>(session));
			session = static_cast<void*>(nullptr);
		}
#endif
		socket.reset();
	}

	// ── Connect factory ───────────────────────────────────────────────────────────

	ne::Task<ne::Result<Client, ne::OsError>> Client::Connect(string_view_t _host, uint16_t _port, const ne::network::SshConfig& _config)
	{
#if !defined(NEBULA_WITH_LIBSSH2)
		(void)_host; (void)_port; (void)_config;
		co_return ne::Result<Client, ne::OsError>::Error(
			ne::OsError(0).Context("[SftpClient/Connect] libssh2 not available"));
#else
		// 1. TCP connect
		auto familyR = co_await ne::network::Socket::ResolveFamily(_host);
		if (familyR.IsError())
			co_return ne::Result<Client, ne::OsError>::Error(
				std::move(familyR.Error()).Context("[SftpClient/Connect/ResolveFamily]"));

		auto sockR = ne::network::Socket::Create(familyR.Value(), SOCK_STREAM, IPPROTO_TCP);
		if (sockR.IsError())
			co_return ne::Result<Client, ne::OsError>::Error(
				std::move(sockR.Error()).Context("[SftpClient/Connect/CreateTcp]"));

		auto connR = co_await sockR.Value().Connect(_host, _port);
		if (connR.IsError())
			co_return ne::Result<Client, ne::OsError>::Error(
				std::move(connR.Error()).Context("[SftpClient/Connect/TcpConnect]"));

		// 2. libssh2 session init
		LIBSSH2_SESSION* sess = ::libssh2_session_init();
		if (!sess)
			co_return ne::Result<Client, ne::OsError>::Error(
				ne::OsError(0).Context("[SftpClient/Connect] libssh2_session_init failed"));

		::libssh2_session_set_blocking(sess, 1);

		const auto fd = static_cast<libssh2_socket_t>(sockR.Value().Handle());
		if (::libssh2_session_handshake(sess, fd) != 0)
		{
			::libssh2_session_free(sess);
			co_return ne::Result<Client, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(::libssh2_session_last_errno(sess)))
				.Context("[SftpClient/Connect/Handshake]"));
		}

		// 3. Authentication
		int authErr = 0;
		if (!_config.privateKeyFile.empty())
		{
			const char* pub = _config.publicKeyFile.empty() ? nullptr : _config.publicKeyFile.c_str();
			const char* priv = _config.privateKeyFile.c_str();
			authErr = ::libssh2_userauth_publickey_fromfile(
				sess, _config.username.c_str(), pub, priv, nullptr);
		}
		else
		{
			authErr = ::libssh2_userauth_password(
				sess, _config.username.c_str(), _config.password.c_str());
		}
		if (authErr != 0)
		{
			::libssh2_session_disconnect(sess, "auth failed");
			::libssh2_session_free(sess);
			co_return ne::Result<Client, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(authErr))
				.Context("[SftpClient/Connect/Auth]"));
		}

		// 4. SFTP subsystem
		LIBSSH2_SFTP* sftp = ::libssh2_sftp_init(sess);
		if (!sftp)
		{
			::libssh2_session_disconnect(sess, "sftp init failed");
			::libssh2_session_free(sess);
			co_return ne::Result<Client, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(::libssh2_session_last_errno(sess)))
				.Context("[SftpClient/Connect/SftpInit]"));
		}

		Client client;
		client.socket = std::move(sockR.Value());
		client.session = static_cast<void*>(sess);
		client.sftpHandle = static_cast<void*>(sftp);
		co_return ne::Result<Client, ne::OsError>::Ok(std::move(client));
#endif
	}

	// ── public methods ────────────────────────────────────────────────────────────

	ne::Task<ne::Result<std::vector<Entry>, ne::OsError>> Client::List(string_view_t _path)
	{
#if !defined(NEBULA_WITH_LIBSSH2)
		(void)_path;
		co_return ne::Result<std::vector<Entry>, ne::OsError>::Error(
			ne::OsError(0).Context("[SftpClient/List] libssh2 not available"));
#else
		LIBSSH2_SFTP* sftp = static_cast<LIBSSH2_SFTP*>(sftpHandle);
		LIBSSH2_SFTP_HANDLE* dir = ::libssh2_sftp_opendir(sftp, string_t(_path).c_str());
		if (!dir)
			co_return ne::Result<std::vector<Entry>, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(
					::libssh2_sftp_last_error(sftp)))
				.Context("[SftpClient/List]"));

		std::vector<Entry> entries;
		char nameBuf[512]{};
		LIBSSH2_SFTP_ATTRIBUTES attrs{};
		while (true)
		{
			const int n = ::libssh2_sftp_readdir(dir, nameBuf, sizeof(nameBuf) - 1, &attrs);
			if (n <= 0) break;
			nameBuf[n] = '\0';
			string_view_t name(nameBuf, static_cast<std::size_t>(n));
			if (name == "." || name == "..") continue;
			Entry e;
			e.name = string_t(name);
			e.isDirectory = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
							LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
			e.size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? attrs.filesize : 0;
			entries.push_back(std::move(e));
		}
		::libssh2_sftp_closedir(dir);
		co_return ne::Result<std::vector<Entry>, ne::OsError>::Ok(std::move(entries));
#endif
	}

	ne::Task<ne::Result<void, ne::OsError>> Client::Get(string_view_t _remote, const SinkFn_t& _sink)
	{
#if !defined(NEBULA_WITH_LIBSSH2)
		(void)_remote; (void)_sink;
		co_return ne::Result<void, ne::OsError>::Error(
			ne::OsError(0).Context("[SftpClient/Get] libssh2 not available"));
#else
		LIBSSH2_SFTP* sftp = static_cast<LIBSSH2_SFTP*>(sftpHandle);
		LIBSSH2_SFTP_HANDLE* fh = ::libssh2_sftp_open(
			sftp, string_t(_remote).c_str(), LIBSSH2_FXF_READ, 0);
		if (!fh)
			co_return ne::Result<void, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(::libssh2_sftp_last_error(sftp)))
				.Context("[SftpClient/Get]"));

		ne::byte_t buf[65536];
		while (true)
		{
			const ssize_t n = ::libssh2_sftp_read(fh, reinterpret_cast<char*>(buf), sizeof(buf));
			if (n < 0)
			{
				::libssh2_sftp_close(fh);
				co_return ne::Result<void, ne::OsError>::Error(
					ne::OsError(static_cast<ne::ulong_t>(::libssh2_sftp_last_error(sftp)))
					.Context("[SftpClient/Get/read]"));
			}
			if (n == 0) break;
			_sink(std::span<const ne::byte_t>(buf, static_cast<std::size_t>(n)));
		}
		::libssh2_sftp_close(fh);
		co_return ne::Result<void, ne::OsError>::Ok();
#endif
	}

	ne::Task<ne::Result<void, ne::OsError>> Client::Put(string_view_t _remote, std::span<const ne::byte_t> _data)
	{
#if !defined(NEBULA_WITH_LIBSSH2)
		(void)_remote; (void)_data;
		co_return ne::Result<void, ne::OsError>::Error(
			ne::OsError(0).Context("[SftpClient/Put] libssh2 not available"));
#else
		LIBSSH2_SFTP* sftp = static_cast<LIBSSH2_SFTP*>(sftpHandle);
		constexpr long mode = LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
							LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH;
		LIBSSH2_SFTP_HANDLE* fh = ::libssh2_sftp_open(
			sftp, string_t(_remote).c_str(),
			LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, mode);
		if (!fh)
			co_return ne::Result<void, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(::libssh2_sftp_last_error(sftp)))
				.Context("[SftpClient/Put]"));

		std::size_t written = 0;
		while (written < _data.size())
		{
			const ssize_t n = ::libssh2_sftp_write(
				fh,
				reinterpret_cast<const char*>(_data.data() + written),
				_data.size() - written);
			if (n < 0)
			{
				::libssh2_sftp_close(fh);
				co_return ne::Result<void, ne::OsError>::Error(
					ne::OsError(static_cast<ne::ulong_t>(::libssh2_sftp_last_error(sftp)))
					.Context("[SftpClient/Put/write]"));
			}
			written += static_cast<std::size_t>(n);
		}
		::libssh2_sftp_close(fh);
		co_return ne::Result<void, ne::OsError>::Ok();
#endif
	}

	ne::Task<ne::Result<void, ne::OsError>> Client::Mkdir(string_view_t _path)
	{
#if !defined(NEBULA_WITH_LIBSSH2)
		(void)_path;
		co_return ne::Result<void, ne::OsError>::Error(
			ne::OsError(0).Context("[SftpClient/Mkdir] libssh2 not available"));
#else
		LIBSSH2_SFTP* sftp = static_cast<LIBSSH2_SFTP*>(sftpHandle);
		constexpr long mode = LIBSSH2_SFTP_S_IRWXU | LIBSSH2_SFTP_S_IRGRP |
							LIBSSH2_SFTP_S_IXGRP | LIBSSH2_SFTP_S_IROTH | LIBSSH2_SFTP_S_IXOTH;
		if (::libssh2_sftp_mkdir(sftp, string_t(_path).c_str(), mode) != 0)
			co_return ne::Result<void, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(::libssh2_sftp_last_error(sftp)))
				.Context("[SftpClient/Mkdir]"));
		co_return ne::Result<void, ne::OsError>::Ok();
#endif
	}

	ne::Task<ne::Result<void, ne::OsError>> Client::Rmdir(string_view_t _path)
	{
#if !defined(NEBULA_WITH_LIBSSH2)
		(void)_path;
		co_return ne::Result<void, ne::OsError>::Error(
			ne::OsError(0).Context("[SftpClient/Rmdir] libssh2 not available"));
#else
		LIBSSH2_SFTP* sftp = static_cast<LIBSSH2_SFTP*>(sftpHandle);
		if (::libssh2_sftp_rmdir(sftp, string_t(_path).c_str()) != 0)
			co_return ne::Result<void, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(::libssh2_sftp_last_error(sftp)))
				.Context("[SftpClient/Rmdir]"));
		co_return ne::Result<void, ne::OsError>::Ok();
#endif
	}

	ne::Task<ne::Result<void, ne::OsError>> Client::Remove(string_view_t _path)
	{
#if !defined(NEBULA_WITH_LIBSSH2)
		(void)_path;
		co_return ne::Result<void, ne::OsError>::Error(
			ne::OsError(0).Context("[SftpClient/Remove] libssh2 not available"));
#else
		LIBSSH2_SFTP* sftp = static_cast<LIBSSH2_SFTP*>(sftpHandle);
		if (::libssh2_sftp_unlink(sftp, string_t(_path).c_str()) != 0)
			co_return ne::Result<void, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(::libssh2_sftp_last_error(sftp)))
				.Context("[SftpClient/Remove]"));
		co_return ne::Result<void, ne::OsError>::Ok();
#endif
	}

	ne::Task<ne::Result<void, ne::OsError>> Client::Rename(string_view_t _from, string_view_t _to)
	{
#if !defined(NEBULA_WITH_LIBSSH2)
		(void)_from; (void)_to;
		co_return ne::Result<void, ne::OsError>::Error(
			ne::OsError(0).Context("[SftpClient/Rename] libssh2 not available"));
#else
		LIBSSH2_SFTP* sftp = static_cast<LIBSSH2_SFTP*>(sftpHandle);
		const string_t src{ _from };
		const string_t dst{ _to };
		if (::libssh2_sftp_rename(sftp, src.c_str(), dst.c_str()) != 0)
			co_return ne::Result<void, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(::libssh2_sftp_last_error(sftp)))
				.Context("[SftpClient/Rename]"));
		co_return ne::Result<void, ne::OsError>::Ok();
#endif
	}

	ne::Task<ne::Result<Entry, ne::OsError>> Client::Stat(string_view_t _path)
	{
#if !defined(NEBULA_WITH_LIBSSH2)
		(void)_path;
		co_return ne::Result<Entry, ne::OsError>::Error(
			ne::OsError(0).Context("[SftpClient/Stat] libssh2 not available"));
#else
		LIBSSH2_SFTP* sftp = static_cast<LIBSSH2_SFTP*>(sftpHandle);
		LIBSSH2_SFTP_ATTRIBUTES attrs{};
		if (::libssh2_sftp_stat(sftp, string_t(_path).c_str(), &attrs) != 0)
			co_return ne::Result<Entry, ne::OsError>::Error(
				ne::OsError(static_cast<ne::ulong_t>(::libssh2_sftp_last_error(sftp)))
				.Context("[SftpClient/Stat]"));

		const auto last = _path.rfind('/');
		Entry e;
		e.name = string_t(last == string_view_t::npos ? _path : _path.substr(last + 1));
		e.isDirectory = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
						LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
		e.size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? attrs.filesize : 0;
		co_return ne::Result<Entry, ne::OsError>::Ok(std::move(e));
#endif
	}

END_NS
