//
// Created by nebula on 24. 5. 29.
//

#ifndef FTPCLIENT_H
#define FTPCLIENT_H

#include <functional>
#include <span>
#include <variant>
#include <vector>

#include "FtpBase.h"
#include "Socket/TcpSocket.h"
#include "Socket/TlsSocket.h"

BEGIN_NS(ne::protocol::Ftp)
	class FtpClient final
	{
		NEBULA_NON_COPYABLE(FtpClient)

	public:
		FtpClient() = delete;
		explicit FtpClient(string_view_t _host, int_t _port = 21, bool_t _isTlsEncrypted = false, bool_t _isImplicit = false);
		~FtpClient();

	private:
		using EitherSocket = std::variant<TcpSocket, TlsSocket>;

		string_t host;
		EitherSocket controlSocket;
		bool_t isTlsEncrypted;
		bool_t isActive = false;
		string_t recvBuffer;
		SocketHandle activeListenHandle;

	public:
		void_t Connect();
		void_t Login(string_view_t _user, string_view_t _password);
		void_t Quit();

	public:
		void_t SetTransferType(TransferType _type);
		void_t SetDataMode(bool_t _isActive) noexcept { isActive = _isActive; }

		[[nodiscard]] string_t Pwd();
		void_t Cwd(string_view_t _path);
		void_t Mkd(string_view_t _path);
		void_t Rmd(string_view_t _path);
		void_t Dele(string_view_t _path);
		void_t Rename(string_view_t _from, string_view_t _to);

		[[nodiscard]] std::vector<Entry> List(string_view_t _path = {});

		void_t Get(string_view_t _remotePath, const std::function<void_t(std::span<const std::byte>)>& _sink, ulonglong_t _offset = 0);
		void_t Put(string_view_t _remotePath, std::span<const std::byte> _data, ulonglong_t _offset = 0);

	private:
		[[nodiscard]] static EitherSocket MakeControlSocket(string_view_t _host, int_t _port, bool_t _isTlsEncrypted, bool_t _isImplicit);

	private:
		[[nodiscard]] Reply SendCommand(string_view_t _command);
		[[nodiscard]] Reply ReadReply();
		[[nodiscard]] string_t ReadLine();

	private:
		[[nodiscard]] EitherSocket OpenPassiveDataConnection();
		void_t PrepareActivePort();
		[[nodiscard]] EitherSocket AcceptActiveData();
		[[nodiscard]] static longlong_t ReadEither(EitherSocket& _socket, std::span<std::byte> _buffer);
		static void_t WriteEither(EitherSocket& _socket, std::span<const std::byte> _data);
	};

END_NS

typedef ne::protocol::Ftp::FtpClient NebulaFtpClient;

#endif //FTPCLIENT_H
