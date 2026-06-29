//
// Created by nebula on 24. 6. 14.
//

#include "Server.h"

#include <cassert>
#include <fstream>
#include <list>
#include "StringFormat.h"
#include "ThreadPool.h"
#include "Socket/TlsSocket.h"
#include "Http/Client/Request.h"



BEGIN_NS(ne::protocol::Http::Server)
	template <std::size_t bufferSize = std::size_t{ 1 } << 12>
	[[nodiscard]]
	Client::Response ReceiveListenSocket(TcpSocket* _socket)
	{
		Client::ResponseCallbacks callbacks;

		auto parser = Client::Parser(callbacks);
		auto buffer = std::array<std::byte, bufferSize>();

		while (true)
		{
			if (auto result = _socket->Read(buffer); result >= 0)
			{
				if (auto parseResult = parser.Parse(std::span(buffer).first(static_cast<std::size_t>(result))))
				{
					auto response = Client::Response(std::move(*parseResult), "");
					if (callbacks.handle) callbacks.handle(response);

					return response;
				}
			}
			else
			{
				throw ne::Exception("[ReceiveListenSocket]", "The peer closed the connection unexpectedly");
			}
		}

		throw ne::Exception("[ReceiveListenSocket]", "Reached an unreachable code path, exiting");
	}

	template <std::size_t bufferSize = std::size_t{ 1 } << 12>
	[[nodiscard]]
	Client::Response ReceiveListenTlsSocket(NebulaTlsSocket* _socket)
	{
		Client::ResponseCallbacks callbacks;

		auto parser = Client::Parser(callbacks);
		auto buffer = std::array<std::byte, bufferSize>();

		while (true)
		{
			if (auto result = _socket->Read(buffer); result >= 0)
			{
				if (auto parseResult = parser.Parse(std::span(buffer).first(static_cast<std::size_t>(result))))
				{
					auto response = Client::Response(std::move(*parseResult), "");
					if (callbacks.handle) callbacks.handle(response);

					return response;
				}
			}
			else
			{
				throw ne::Exception("[ReceiveListenTlsSocket]", "The peer closed the connection unexpectedly");
			}
		}

		throw ne::Exception("[ReceiveListenTlsSocket]", "Reached an unreachable code path, exiting");
	}



	/*--------------------------------------------------*/



	Server::Server(const string_view_t _server, const int_t _port)
		: serverSocket(OpenSocket(_server, _port, false))
		, isTlsEncrypted(false)

	{
	}

#if defined(WIN32)
	Server::Server(const string_view_t _server, const int_t _port, string_view_t _pfx, string_view_t _password)
#elif defined(IS_POSIX)
	Server::Server(const string_view_t _server, const int_t _port, string_view_t _crt, string_view_t _key)
#endif
		: serverSocket(OpenSocket(_server, _port, true))
		, isTlsEncrypted(true)
	{
#if defined(_WIN32)
		LoadCertificates(_pfx, _password);
#elif defined(IS_POSIX)
		serverSocket.Init();
		serverSocket.LoadCertificates(_crt, _key);
#endif
	}



	void_t Server::Listen(const bool_t _isNonblocking)
	{
		using namespace std::string_view_literals;
		using enum Method;

		try
		{
			serverSocket.Listen();

			auto tasks = std::make_unique<ThreadPool>(threadPoolSize);
			while (true)
			{
				auto socket = serverSocket.Accept();
#if defined(_WIN32)
				if (socket == INVALID_SOCKET)
				{
					// error log
					break; // continue;
				}
#elif defined(IS_POSIX)
				if (socket == -1)
				{
					// error log
					break; // continue;
				}
#endif

				tasks->Enqueue([this, socket]
				{
					Response response;

					if (isTlsEncrypted)
					{
						auto listenSocket = std::make_unique<NebulaTlsSocket>(socket);
						listenSocket->SetTimeout(requestTimeout);

						try
						{
#if defined(_WIN32)
							listenSocket->Handshake(certContext);
#elif defined(IS_POSIX)
							listenSocket->Handshake(serverSocket.GetTlsContext());
#endif
							auto listenResponse = ReceiveListenTlsSocket<>(listenSocket.get());
							SetResponse(listenResponse.GetMethod(), std::move(listenResponse), std::move(response));
						} catch (const ne::Exception& _e)
						{
							response.SetStatusCode(StatusCode::BAD_REQUEST);
						} catch (...)
						{
							response.SetStatusCode(StatusCode::INTERNAL_SERVER_ERROR);
						}
						listenSocket->Write(response.GetResponseString());
					}
					else
					{
						auto listenSocket = std::make_unique<TcpSocket>(socket);
						listenSocket->SetTimeout(requestTimeout);

						try
						{
							auto listenResponse = ReceiveListenSocket<>(listenSocket.get());
							SetResponse(listenResponse.GetMethod(), std::move(listenResponse), std::move(response));
						} catch (const ne::Exception& _e)
						{
							response.SetStatusCode(StatusCode::BAD_REQUEST);
						} catch (...)
						{
							response.SetStatusCode(StatusCode::INTERNAL_SERVER_ERROR);
						}
						listenSocket->Write(response.GetResponseString());
					}
				});
			}
			tasks->Shutdown();
		} catch (const ne::Exception& _e)
		{
		}
	}



	Server& Server::Route(const Method _method, const string_view_t _path, const Handler& _handler)
	{
		using enum Method;
		switch (_method)
		{
		case GET: getHandlers.emplace_back(_path, _handler);
			break;
		case POST: postHandlers.emplace_back(_path, _handler);
			break;
		case PUT: putHandlers.emplace_back(_path, _handler);
			break;
		case DEL: deleteHandlers.emplace_back(_path, _handler);
			break;
		case OPTIONS: optionsHandlers.emplace_back(_path, _handler);
			break;
		case PATCH: patchHandlers.emplace_back(_path, _handler);
			break;

		case CONNECT:
		case HEAD:
		case TRACE: break;
		}

		return *this;
	}

	Server& Server::SetTimeout(const std::chrono::milliseconds _timeout)
	{
		requestTimeout = _timeout;

		return *this;
	}



#if defined(WIN32)
	void_t Server::LoadCertificates(string_view_t _pfx, string_view_t _password)
	{
		std::ifstream file(_pfx.data(), std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			throw ne::Exception("[Server/LoadCertificates]", std::format("Invalid file path (path: {})", _pfx.data()));
		}

		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);

		std::vector<byte_t> buffer(size);
		if (!file.read((char_t*)buffer.data(), size))
		{
			throw ne::Exception("[Server/LoadCertificates]", std::format("Failed to read pfx file (error: {})", strerror(errno)));
		}

		CRYPT_DATA_BLOB pfxBlob = { static_cast<ulong_t>(buffer.size()), (byte_t*)buffer.data() };

		string_t temp = string_t(_password);
		std::wstring password;
		password.assign(temp.begin(), temp.end());

		HCERTSTORE certStore = PFXImportCertStore(&pfxBlob, password.c_str(), 0);
		if (certStore == INVALID_HANDLE_VALUE)
		{
			throw ne::Exception("[Server/LoadCertificates]", std::format("Failed to PFXImportCertStore function (error: {})", GetLastError()));
		}

		certContext = CertEnumCertificatesInStore(certStore, nullptr);
		CertCloseStore(certStore, 0);
	}
#endif

	void_t Server::SetResponse(const Method _method, const Client::Response&& _clientResponse, Response&& _serverResponse)
	{
		using enum Method;
		using enum StatusCode;

		for (const auto& header : _clientResponse.GetHeaders())
		{
			if (StringFormat::Lower(string_t(header.name)) != "content-type") continue;

			if (StringFormat::Lower(string_t(header.value)) != "application/json")
			{
				_serverResponse.SetStatusCode(UNSUPPORTED_MEDIA_TYPE);
				return;
			}
		}

		if (_clientResponse.GetUri().size() > 8192)
		{ // 8K
			_serverResponse.SetStatusCode(URI_TOO_LONG);
			return;
		}
		if (_clientResponse.GetBody().size() > 102400)
		{ // 100K
			_serverResponse.SetStatusCode(PAYLOAD_TOO_LARGE);
			return;
		}

		const Handlers* handlers = nullptr;
		switch (_method)
		{
		case GET: handlers = &getHandlers;
			break;
		case POST: handlers = &postHandlers;
			break;
		case PUT: handlers = &putHandlers;
			break;
		case DEL: handlers = &deleteHandlers;
			break;
		case OPTIONS: handlers = &optionsHandlers;
			break;
		case PATCH: handlers = &patchHandlers;
			break;

		case CONNECT:
		case HEAD:
		case TRACE: _serverResponse.SetStatusCode(METHOD_NOT_ALLOWED);
			return;
		}

		if (handlers == nullptr)
		{
			_serverResponse.SetStatusCode(INTERNAL_SERVER_ERROR);
			return;
		}

		if (!std::ranges::any_of((*handlers).begin(), (*handlers).end(), [&_clientResponse, &_serverResponse](const auto& _handler)
		{
			if (_clientResponse.GetPath().compare(_handler.first) != 0) return false;

			_handler.second(std::move(_serverResponse));
			return true;
		}))
		{
			_serverResponse.SetStatusCode(NOT_FOUND);
			return;
		};

		_serverResponse.SetStatusCode(OK);
	}

END_NS
