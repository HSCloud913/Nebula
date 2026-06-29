//
// Created by nebula on 24. 5. 29.
//

#ifndef HTTPCLIENTSOCKET_H
#define HTTPCLIENTSOCKET_H

#include <variant>
#include <memory>
#include <chrono>

#include "Http/HttpUtil.h"
#include "Socket/SocketBase.h"

BEGIN_NS(ne::protocol::Http::Client)
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
		friend Socket GetSocket(socket_t _socket, bool_t _isTlsEncrypted);

	public:
		void_t Connect() const;
		void_t SetTimeout(std::chrono::milliseconds _timeout) const;
		[[nodiscard]] bool_t IsAlive() const;
		[[nodiscard]] longlong_t Read(std::span<std::byte> buffer) const;
		[[nodiscard]] std::vector<std::byte> Read(std::size_t _bytes = 512) const;
		void_t Write(std::span<const std::byte> _data) const;
		void_t Write(string_view_t _stringView) const;
	};



	[[nodiscard]]
	inline Socket OpenSocket(const string_view_t _server, const int_t _port, const bool_t _isTlsEncrypted)
	{
		return Socket{ _server, _port, _isTlsEncrypted };
	}

END_NS

typedef ne::protocol::Http::Client::Socket NebulaHttpClientSocket;

#endif //HTTPCLIENTSOCKET_H
