//
// Created by nebula on 24. 5. 29.
//

#ifndef SFTPCLIENT_H
#define SFTPCLIENT_H

#include <functional>
#include <span>
#include <vector>

#include "SftpBase.h"
#include "NebulaHandle.h"
#include "Socket/TcpSocket.h"
#include <libssh2.h>
#include <libssh2_sftp.h>

BEGIN_NS(ne::protocol::Sftp)
	class Client final
	{
		NEBULA_NON_COPYABLE(Client)

	public:
		Client() = delete;
		explicit Client(string_view_t _host, int_t _port = 22);
		~Client();

	private:
		using SessionHandle = ne::Handle<LIBSSH2_SESSION*, decltype([](const auto _s) { ::libssh2_session_free(_s); })>;
		using SftpHandle = ne::Handle<LIBSSH2_SFTP*, decltype([](const auto _s) { ::libssh2_sftp_shutdown(_s); })>;

		string_t host;
		int_t port;
		TcpSocket socket;
		SessionHandle session;
		SftpHandle sftp;

	public:
		void_t Connect();
		void_t VerifyKnownHosts(string_view_t _knownHostsPath);
		void_t AuthPassword(string_view_t _user, string_view_t _password);
		void_t AuthPublicKey(string_view_t _user, string_view_t _publicKeyPath, string_view_t _privateKeyPath, string_view_t _passphrase = {});
		void_t Disconnect();

	public:
		[[nodiscard]] std::vector<Entry> List(string_view_t _path);
		void_t Get(string_view_t _remotePath, const std::function<void_t(std::span<const std::byte>)>& _sink);
		void_t Put(string_view_t _remotePath, std::span<const std::byte> _data);

		void_t Mkdir(string_view_t _path);
		void_t Rmdir(string_view_t _path);
		void_t Remove(string_view_t _path);
		void_t Rename(string_view_t _from, string_view_t _to);
		[[nodiscard]] Entry Stat(string_view_t _path);

	private:
		void_t OpenSftpChannel();
	};

END_NS

typedef ne::protocol::Sftp::Client NebulaSftpClient;

#endif //SFTPCLIENT_H
