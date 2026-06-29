//
// Created by nebula on 24. 5. 29.
//

#ifndef TLSSOCKET_H
#define TLSSOCKET_H

#include "TcpSocket.h"
#if defined(_WIN32)
#	include "Windows/Secure/SchannelBase.h"
#	include "Windows/Secure/TlsMessageBuffer.h"
#elif defined(IS_POSIX)
#	include <openssl/ssl.h>
#	include <openssl/err.h>
#endif

BEGIN_NS(ne::protocol)
#if defined(IS_POSIX)
	[[nodiscard]]
	inline string_t OpenSslError()
	{
		using UniqueBio = std::unique_ptr<BIO, decltype([](BIO* _x)
		{
			::BIO_free(_x);
		})>;

		const auto bioHandle = UniqueBio{ ::BIO_new(::BIO_s_mem()) };
		::ERR_print_errors(bioHandle.get());

		auto buffer = static_cast<char_t*>(nullptr);
		const auto length = ::BIO_get_mem_data(bioHandle.get(), &buffer);

		return string_t(static_cast<char_t*>(buffer), length);
	}
#endif

	class TlsSocket final
	{
		using ReadHandler      = std::function<void(std::vector<std::byte>)>;
		using ExceptionHandler = std::function<void(const std::string&)>;

	public:
		explicit TlsSocket(const socket_t _socket) : socket(std::make_unique<TcpSocket>(_socket)) {}
		TlsSocket(socket_t _socket, string_view_t _server);
		TlsSocket(string_view_t _server, int_t _port);

#if defined(IS_POSIX)
	private:
		using TlsContext = std::unique_ptr<SSL_CTX, decltype([](const auto _x)
		{
			::SSL_CTX_free(_x);
		})>;
		using TlsConnection = std::unique_ptr<SSL, decltype([](const auto _x)
		{
			::SSL_free(_x);
		})>;

	private:
		void_t InitializeClientContext();
#endif

	private:
		std::unique_ptr<TcpSocket> socket;
		string_view_t server;

#if defined(_WIN32)
		SecurityContextHandle securityContextHandle;
		SecPkgContext_StreamSizes streamSizes{};
		TlsMessageBuffer tlsBuffer;
		std::span<const std::byte> decryptedMessage;
#elif defined(IS_POSIX)
		TlsContext tlsContext;
		TlsConnection tlsConnection;
		bool_t isClosed = false;
#endif

		ReadHandler      readHandler;
		ExceptionHandler exceptionHandler;

	public:
		void_t Connect();
		void_t Reconnect();
		[[nodiscard]] bool_t IsConnected() const { return socket->IsConnected(); }
		[[nodiscard]] bool_t IsAlive() const { return socket->IsAlive(); }
		[[nodiscard]] socket_t GetHandle() const { return socket->GetHandle(); }
		void_t SetTimeout(const std::chrono::milliseconds _timeout) const { socket->SetTimeout(_timeout); }
		void_t Bind() const { socket->Bind(); }
		void_t Listen() const { socket->Listen(); }
		[[nodiscard]] socket_t Accept() const { return socket->Accept(); };

		void_t Select() const { socket->Select(); }
		void_t Poll() const { socket->Poll(); }
#if defined(_WIN32)
		void_t Iocp();
#elif defined(__linux__)
		void_t Epoll();
#elif defined(__APPLE__)
		void_t Kqueue();
#endif

	public:
		void_t RegisterReadHandler(const ReadHandler& _readHandler) noexcept { readHandler = _readHandler; }
		void_t RegisterExceptionHandler(const ExceptionHandler& _exceptionHandler) noexcept { exceptionHandler = _exceptionHandler; }

public:
		void_t Handshake();
#if defined(_WIN32)
		void_t Handshake(PCCERT_CONTEXT _certContext);
#elif defined(IS_POSIX)
		void_t Certificates(string_view_t _crt, string_view_t _key);
		void_t Handshake(SSL_CTX* _tlsContext);
#endif

	public:
		[[nodiscard]] longlong_t Read(std::span<std::byte> _buffer);
		bool_t Write(std::span<const std::byte> _data);

#if defined(_WIN32)
	private:
		[[nodiscard]] std::vector<std::byte> EncryptMessage(const std::span<const std::byte> _data);
		[[nodiscard]] bool_t DecryptMessage(const std::span<std::byte> _message);
		[[nodiscard]] longlong_t ReadEncryptedData(std::size_t _offset);
#elif defined(IS_POSIX)
	public:
		[[nodiscard]] SSL_CTX* GetTlsContext() const { return tlsContext.get(); }
#endif
	};

END_NS

typedef ne::protocol::TlsSocket NebulaTlsSocket;

#endif //TLSSOCKET_H
