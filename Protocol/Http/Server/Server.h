//
// Created by nebula on 24. 6. 14.
//

#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <functional>
#include <chrono>

#include "Socket.h"
#include "Http/HttpBase.h"
#include "Http/Client/Response.h"
#include "Response.h"

BEGIN_NS(ne::protocol::Http::Server)
	class Server
	{
		NEBULA_NON_COPYABLE_MOVABLE(Server)

	public:
		Server(const string_view_t _server, const int_t _port);
#if defined(WIN32)
		Server(const string_view_t _server, const int_t _port, string_view_t _pfx, string_view_t _password);
#elif defined(IS_POSIX)
		Server(const string_view_t _server, const int_t _port, string_view_t _crt, string_view_t _key);
#endif
		~Server() = default;

	private:
		using Handler = std::function<void_t(Response&&)>;
		using Handlers = std::vector<std::pair<string_t, Handler>>;

#if defined(WIN32)
	private:
		PCCERT_CONTEXT certContext;
#endif

	private:
		NebulaHttpServerSocket serverSocket;
		bool_t isTlsEncrypted;
		std::chrono::milliseconds requestTimeout{ 0 };
		size_t threadPoolSize = 10;

		Handlers getHandlers;
		Handlers postHandlers;
		Handlers putHandlers;
		Handlers deleteHandlers;
		Handlers optionsHandlers;
		Handlers patchHandlers;

	public:
		void_t Listen(const bool_t _isNonblocking);

	public:
		Server& Route(const Method _method, const string_view_t _path, const Handler& _handler);
		Server& SetTimeout(std::chrono::milliseconds _timeout);
		Server& SetThreadPoolSize(size_t _size) noexcept { threadPoolSize = _size; return *this; }

	private:
#if defined(WIN32)
		void_t LoadCertificates(string_view_t _pfx, string_view_t _password);
#endif
		void_t SetResponse(const Method _method, const Client::Response&& _listenResponse, Response&& _response);
	};

END_NS

typedef ne::protocol::Http::Server::Server NebulaHttpServer;

#endif //HTTPSERVER_H
