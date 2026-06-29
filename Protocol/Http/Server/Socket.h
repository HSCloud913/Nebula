//
// Created by nebula on 24. 6. 12.
//

#ifndef HTTPSERVERSOCKET_H
#define HTTPSERVERSOCKET_H

#include <memory>
#include "Socket/SocketBase.h"
#if defined(IS_POSIX)
#	include <openssl/ssl.h>
#endif

BEGIN_NS(ne::protocol::Http::Server)
	class Socket final
	{
		NEBULA_NON_COPYABLE(Socket)

	public:
		Socket() = delete;
		~Socket();

		Socket(Socket&&) noexcept;
		Socket& operator=(Socket&&) noexcept;

	private:
		class Impl;
		std::unique_ptr<Impl> impl;
		Socket(string_view_t _server, int_t _port, bool_t _isTlsEncrypted);

	private:
		friend Socket OpenSocket(string_view_t _server, int_t _port, bool_t _isTlsEncrypted);

	public:
		void_t Listen() const;
		[[nodiscard]] socket_t Accept() const;
#if defined(IS_POSIX)
		void_t Init() const;
		void_t LoadCertificates(string_view_t _crt, string_view_t _key) const;
		[[nodiscard]] SSL_CTX* GetTlsContext() const;
#endif
	};



	[[nodiscard]]
	inline Socket OpenSocket(string_view_t _server, const int_t _port, const bool_t _isTlsEncrypted)
	{
		return Socket{ _server, _port, _isTlsEncrypted };
	}

END_NS

typedef ne::protocol::Http::Server::Socket NebulaHttpServerSocket;

#endif //HTTPSERVERSOCKET_H
