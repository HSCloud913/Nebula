//
// Created by nebula on 24. 5. 29.
//

#include "SftpClient.h"

#include <array>
#include "Exception.h"

namespace
{
	ne::void_t EnsureLibssh2Initialized()
	{
		static const auto initialized = []
		{
			if (::libssh2_init(0) != 0)
			{
				throw ne::Exception("[SftpClient]", "Failed to libssh2_init function");
			}
			return true;
		}();
		static_cast<void>(initialized);
	}
}

BEGIN_NS(ne::protocol::Sftp)
	namespace
	{
		using FileHandle = ne::Handle<LIBSSH2_SFTP_HANDLE*, decltype([](const auto _h) { ::libssh2_sftp_close_handle(_h); })>;

		[[nodiscard]] Entry ToEntry(string_t _name, const LIBSSH2_SFTP_ATTRIBUTES& _attributes)
		{
			auto entry = Entry{ .name = std::move(_name) };
			if (_attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) entry.isDirectory = LIBSSH2_SFTP_S_ISDIR(_attributes.permissions);
			if (_attributes.flags & LIBSSH2_SFTP_ATTR_SIZE) entry.size = _attributes.filesize;

			return entry;
		}
	}



	Client::Client(const string_view_t _host, const int_t _port)
		: host(_host)
		, port(_port)
		, socket(_host, _port)
	{
		EnsureLibssh2Initialized();
	}

	Client::~Client()
	{
		Disconnect();
	}



	void_t Client::Connect()
	{
		socket.Connect();

		session = SessionHandle(::libssh2_session_init());
		if (!session)
		{
			throw ne::Exception("[SftpClient/Connect]", "Failed to libssh2_session_init function");
		}

		if (::libssh2_session_handshake(session.Get(), socket.GetHandle()) != 0)
		{
			throw ne::Exception("[SftpClient/Connect]", "Failed to libssh2_session_handshake function");
		}
	}

	void_t Client::VerifyKnownHosts(const string_view_t _knownHostsPath)
	{
		using KnownHostsHandle = std::unique_ptr<LIBSSH2_KNOWNHOSTS, decltype([](auto _kh) { ::libssh2_knownhost_free(_kh); })>;

		auto kh = KnownHostsHandle(::libssh2_knownhost_init(session.Get()));
		if (!kh)
			throw ne::Exception("[SftpClient/VerifyKnownHosts]", "Failed to initialize known hosts context");

		const auto path = string_t(_knownHostsPath);
		if (::libssh2_knownhost_readfile(kh.get(), path.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH) < 0)
			throw ne::Exception("[SftpClient/VerifyKnownHosts]", std::format("Failed to read known_hosts file ({})", _knownHostsPath));

		int_t keyType = 0;
		size_t keyLen = 0;
		const char_t* hostKey = ::libssh2_session_hostkey(session.Get(), &keyLen, &keyType);
		if (!hostKey)
			throw ne::Exception("[SftpClient/VerifyKnownHosts]", "Failed to retrieve server host key");

		const int_t typeMask = [&]
		{
			switch (keyType)
			{
			case LIBSSH2_HOSTKEY_TYPE_RSA:       return LIBSSH2_KNOWNHOST_KEY_SSHRSA  | LIBSSH2_KNOWNHOST_KEYENC_RAW;
			case LIBSSH2_HOSTKEY_TYPE_DSS:       return LIBSSH2_KNOWNHOST_KEY_SSHDSS  | LIBSSH2_KNOWNHOST_KEYENC_RAW;
			case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return LIBSSH2_KNOWNHOST_KEY_ECDSA_256 | LIBSSH2_KNOWNHOST_KEYENC_RAW;
			case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return LIBSSH2_KNOWNHOST_KEY_ECDSA_384 | LIBSSH2_KNOWNHOST_KEYENC_RAW;
			case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return LIBSSH2_KNOWNHOST_KEY_ECDSA_521 | LIBSSH2_KNOWNHOST_KEYENC_RAW;
			case LIBSSH2_HOSTKEY_TYPE_ED25519:   return LIBSSH2_KNOWNHOST_KEY_ED25519  | LIBSSH2_KNOWNHOST_KEYENC_RAW;
			default:                             return LIBSSH2_KNOWNHOST_KEYENC_RAW;
			}
		}();

		libssh2_knownhost* found = nullptr;
		const int_t result = ::libssh2_knownhost_checkp(
			kh.get(),
			host.c_str(), port,
			hostKey, keyLen,
			LIBSSH2_KNOWNHOST_TYPE_PLAIN | typeMask,
			&found);

		switch (result)
		{
		case LIBSSH2_KNOWNHOST_CHECK_MATCH:    return;
		case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND: throw ne::Exception("[SftpClient/VerifyKnownHosts]", std::format("Host not found in known_hosts: {}", host));
		case LIBSSH2_KNOWNHOST_CHECK_MISMATCH: throw ne::Exception("[SftpClient/VerifyKnownHosts]", std::format("Host key mismatch — possible MITM attack: {}", host));
		default:                               throw ne::Exception("[SftpClient/VerifyKnownHosts]", "Failed to verify host key");
		}
	}

	void_t Client::AuthPassword(const string_view_t _user, const string_view_t _password)
	{
		if (::libssh2_userauth_password_ex(
			session.Get(),
			_user.data(), static_cast<unsigned int>(_user.size()),
			_password.data(), static_cast<unsigned int>(_password.size()),
			nullptr
		) != 0)
		{
			throw ne::Exception("[SftpClient/AuthPassword]", "Password authentication failed");
		}

		OpenSftpChannel();
	}

	void_t Client::AuthPublicKey(const string_view_t _user, const string_view_t _publicKeyPath, const string_view_t _privateKeyPath, const string_view_t _passphrase)
	{
		const auto publicKeyPath = string_t(_publicKeyPath);
		const auto privateKeyPath = string_t(_privateKeyPath);
		const auto passphrase = string_t(_passphrase);

		if (::libssh2_userauth_publickey_fromfile_ex(
			session.Get(),
			_user.data(), static_cast<unsigned int>(_user.size()),
			publicKeyPath.c_str(),
			privateKeyPath.c_str(),
			passphrase.empty() ? nullptr : passphrase.c_str()
		) != 0)
		{
			throw ne::Exception("[SftpClient/AuthPublicKey]", "Public key authentication failed");
		}

		OpenSftpChannel();
	}

	void_t Client::Disconnect()
	{
		sftp = SftpHandle{};

		if (session)
		{
			::libssh2_session_disconnect(session.Get(), "Normal shutdown");
			session = SessionHandle{};
		}
	}

	void_t Client::OpenSftpChannel()
	{
		sftp = SftpHandle(::libssh2_sftp_init(session.Get()));
		if (!sftp)
		{
			throw ne::Exception("[SftpClient/OpenSftpChannel]", "Failed to libssh2_sftp_init function");
		}
	}



	std::vector<Entry> Client::List(const string_view_t _path)
	{
		const auto path = string_t(_path);
		auto handle = FileHandle(::libssh2_sftp_opendir(sftp.Get(), path.c_str()));
		if (!handle)
		{
			throw ne::Exception("[SftpClient/List]", std::format("Failed to open remote directory ({})", _path));
		}

		auto entries = std::vector<Entry>();
		auto nameBuffer = std::array<char_t, 512>();
		auto longEntryBuffer = std::array<char_t, 512>();

		while (true)
		{
			auto attributes = LIBSSH2_SFTP_ATTRIBUTES{};
			const auto length = ::libssh2_sftp_readdir_ex(handle.Get(), nameBuffer.data(), nameBuffer.size(), longEntryBuffer.data(), longEntryBuffer.size(), &attributes);
			if (length <= 0) break;

			auto name = string_t(nameBuffer.data(), static_cast<std::size_t>(length));
			if (name == "." || name == "..") continue;

			entries.push_back(ToEntry(std::move(name), attributes));
		}

		return entries;
	}

	void_t Client::Get(const string_view_t _remotePath, const std::function<void_t(std::span<const std::byte>)>& _sink)
	{
		const auto path = string_t(_remotePath);
		auto handle = FileHandle(::libssh2_sftp_open(sftp.Get(), path.c_str(), LIBSSH2_FXF_READ, 0));
		if (!handle)
		{
			throw ne::Exception("[SftpClient/Get]", std::format("Failed to open remote file ({})", _remotePath));
		}

		auto buffer = std::array<char_t, 8192>();
		while (true)
		{
			const auto received = ::libssh2_sftp_read(handle.Get(), buffer.data(), buffer.size());
			if (received < 0)
			{
				throw ne::Exception("[SftpClient/Get]", std::format("Failed to read from remote file ({})", _remotePath));
			}
			if (received == 0) break;

			_sink(std::span(reinterpret_cast<const std::byte*>(buffer.data()), static_cast<std::size_t>(received)));
		}
	}

	void_t Client::Put(const string_view_t _remotePath, std::span<const std::byte> _data)
	{
		const auto path = string_t(_remotePath);
		auto handle = FileHandle(::libssh2_sftp_open(sftp.Get(), path.c_str(), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0644));
		if (!handle)
		{
			throw ne::Exception("[SftpClient/Put]", std::format("Failed to open remote file for writing ({})", _remotePath));
		}

		while (!_data.empty())
		{
			const auto written = ::libssh2_sftp_write(handle.Get(), reinterpret_cast<const char_t*>(_data.data()), _data.size());
			if (written < 0)
			{
				throw ne::Exception("[SftpClient/Put]", std::format("Failed to write to remote file ({})", _remotePath));
			}

			_data = _data.subspan(static_cast<std::size_t>(written));
		}
	}



	void_t Client::Mkdir(const string_view_t _path)
	{
		const auto path = string_t(_path);
		if (::libssh2_sftp_mkdir(sftp.Get(), path.c_str(), 0755) != 0)
		{
			throw ne::Exception("[SftpClient/Mkdir]", std::format("Failed to create remote directory ({})", _path));
		}
	}

	void_t Client::Rmdir(const string_view_t _path)
	{
		const auto path = string_t(_path);
		if (::libssh2_sftp_rmdir(sftp.Get(), path.c_str()) != 0)
		{
			throw ne::Exception("[SftpClient/Rmdir]", std::format("Failed to remove remote directory ({})", _path));
		}
	}

	void_t Client::Remove(const string_view_t _path)
	{
		const auto path = string_t(_path);
		if (::libssh2_sftp_unlink(sftp.Get(), path.c_str()) != 0)
		{
			throw ne::Exception("[SftpClient/Remove]", std::format("Failed to remove remote file ({})", _path));
		}
	}

	void_t Client::Rename(const string_view_t _from, const string_view_t _to)
	{
		const auto from = string_t(_from);
		const auto to = string_t(_to);
		if (::libssh2_sftp_rename(sftp.Get(), from.c_str(), to.c_str()) != 0)
		{
			throw ne::Exception("[SftpClient/Rename]", std::format("Failed to rename remote path ({} -> {})", _from, _to));
		}
	}

	Entry Client::Stat(const string_view_t _path)
	{
		const auto path = string_t(_path);
		auto attributes = LIBSSH2_SFTP_ATTRIBUTES{};
		if (::libssh2_sftp_stat(sftp.Get(), path.c_str(), &attributes) != 0)
		{
			throw ne::Exception("[SftpClient/Stat]", std::format("Failed to stat remote path ({})", _path));
		}

		return ToEntry(string_t(_path), attributes);
	}

END_NS
