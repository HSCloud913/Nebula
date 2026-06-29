//
// Created by hscloud on 25. 6. 29.
//

#include "Http1Server.h"
#include "Socket/Socket.h"
#include "Stream/PlainStream.h"
#include "Coroutine/Awaitable.h"
#include <array>
#include <span>
#include <string>

BEGIN_NS(ne::network::http_1)
	Server::Server(IIoEngine& _engine, RequestHandler _handler) noexcept
		: engine(&_engine)
		, handler(std::move(_handler)) {}

	void Server::Stop() noexcept
	{
		running = false;
	}



	ne::Task<ne::Result<void, ne::OsError>> Server::Run(const string_view_t _host, const uint16_t _port)
	{
		auto listenerRes = Socket::CreateTcp();
		if (listenerRes.IsError())
		{
			auto err = std::move(listenerRes.Error());
			err.Context("[Server/Run]");
			co_return ne::Result<void, ne::OsError>::Error(std::move(err));
		}

		Socket listener = std::move(listenerRes.Value());

		if (auto r = listener.SetReuseAddr(true); r.IsError())
		{
			auto err = std::move(r.Error());
			err.Context("[Server/Run]");
			co_return ne::Result<void, ne::OsError>::Error(std::move(err));
		}

		if (auto r = listener.Bind(_host, _port); r.IsError())
		{
			auto err = std::move(r.Error());
			err.Context("[Server/Run]");
			co_return ne::Result<void, ne::OsError>::Error(std::move(err));
		}

		if (auto r = listener.Listen(); r.IsError())
		{
			auto err = std::move(r.Error());
			err.Context("[Server/Run]");
			co_return ne::Result<void, ne::OsError>::Error(std::move(err));
		}

		if (auto r = listener.SetNonBlocking(true); r.IsError())
		{
			auto err = std::move(r.Error());
			err.Context("[Server/Run]");
			co_return ne::Result<void, ne::OsError>::Error(std::move(err));
		}

		running = true;
		while (running)
		{
			// 새 연결 대기 — 리스너 소켓이 readable 이 되면 Accept 가능
			auto eventRes = co_await RecvAwaitable{ listener.Handle(), *engine };
			if (eventRes.IsError()) break;

			auto sockRes = listener.Accept();
			if (sockRes.IsError()) continue; // EAGAIN 등 일시적 에러는 무시

			// 순차 처리: 이 연결이 끝난 뒤 다음 연결 수락
			co_await HandleConnection(std::move(sockRes.Value()));
		}

		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Task<void> Server::HandleConnection(Socket _sock)
	{
		if (_sock.SetNonBlocking(true).IsError()) co_return;

		auto streamRes = PlainStream::Create(std::move(_sock), *engine);
		if (streamRes.IsError()) co_return;

		PlainStream& stream = streamRes.Value();

		// 헤더 끝 (\r\n\r\n) 까지 누적 수신
		string_t raw;
		raw.reserve(4096);
		std::array<byte_t, 4096> buf{};

		while (raw.find("\r\n\r\n") == string_t::npos)
		{
			auto r = co_await stream.Receive(std::span{ buf });
			if (r.IsError() || r.Value() == 0) co_return;
			raw.append(reinterpret_cast<const char*>(buf.data()), r.Value());
		}

		// Content-Length 만큼 바디 추가 수신
		const auto headerEnd = raw.find("\r\n\r\n");
		const auto contentLen = ParseContentLength(string_view_t{ raw.data(), headerEnd });
		const auto totalNeeded = headerEnd + 4 + contentLen;

		while (raw.size() < totalNeeded)
		{
			auto r = co_await stream.Receive(std::span{ buf });
			if (r.IsError() || r.Value() == 0) break;
			raw.append(reinterpret_cast<const char*>(buf.data()), r.Value());
		}

		auto reqRes = ParseRequest(raw);
		if (reqRes.IsError()) co_return;

		HttpResponse resp = co_await handler(reqRes.Value());

		const string_t respStr = SerializeResponse(resp);
		const auto sendSpan = std::span{
			reinterpret_cast<const byte_t*>(respStr.data()),
			respStr.size()
		};

		(void)co_await stream.Send(sendSpan);
		(void)stream.Close();
	}



	ne::Result<HttpRequest, ne::OsError> Server::ParseRequest(const string_view_t _raw)
	{
		const auto sep = _raw.find("\r\n\r\n");
		if (sep == string_view_t::npos) return ne::Result<HttpRequest, ne::OsError>::Error(ne::OsError{ 0, "incomplete HTTP request" });

		HttpRequest req;

		// Request line: "METHOD SP PATH SP HTTP/1.1"
		const auto lineEnd = _raw.find("\r\n");
		const auto requestLine = _raw.substr(0, lineEnd);

		const auto sp1 = requestLine.find(' ');
		if (sp1 == string_view_t::npos) return ne::Result<HttpRequest, ne::OsError>::Error(ne::OsError{ 0, "malformed request line" });

		req.method = ParseMethod(requestLine.substr(0, sp1));

		const auto sp2 = requestLine.find(' ', sp1 + 1);
		req.path = string_t(
			(sp2 != string_view_t::npos)
				? requestLine.substr(sp1 + 1, sp2 - sp1 - 1)
				: requestLine.substr(sp1 + 1)
		);

		// Headers
		auto pos = lineEnd + 2;
		while (pos < sep)
		{
			const auto end = _raw.find("\r\n", pos);
			if (end == string_view_t::npos || end > sep) break;

			const auto colon = _raw.find(':', pos);
			if (colon != string_view_t::npos && colon < end)
			{
				string_t name(_raw.substr(pos, colon - pos));

				auto valueStart = colon + 1;
				while (valueStart < end && _raw[valueStart] == ' ') ++valueStart;
				string_t value(_raw.substr(valueStart, end - valueStart));

				req.headers.emplace_back(std::move(name), std::move(value));
			}
			pos = end + 2;
		}

		req.body = string_t(_raw.substr(sep + 4));
		return ne::Result<HttpRequest, ne::OsError>::Ok(std::move(req));
	}

	std::size_t Server::ParseContentLength(const string_view_t _headers) noexcept
	{
		const auto pos = _headers.find("Content-Length: ");
		if (pos == string_view_t::npos) return 0;

		const auto start = pos + 16; // "Content-Length: " 길이
		const auto end = _headers.find("\r\n", start);
		const auto numStr = (end != string_view_t::npos)
								? _headers.substr(start, end - start)
								: _headers.substr(start);

		std::size_t len = 0;
		for (const char c : numStr)
		{
			if (c < '0' || c > '9') break;
			len = len * 10 + static_cast<std::size_t>(c - '0');
		}
		return len;
	}

	string_t Server::SerializeResponse(const HttpResponse& _response)
	{
		string_t s;
		s.reserve(256);
		s += "HTTP/1.1 ";
		s += std::to_string(_response.status);
		s += ' ';
		s += _response.statusText.empty()
				? string_t(DefaultStatusText(_response.status))
				: _response.statusText;
		s += "\r\n";

		bool_t hasContentLength = false;
		for (const auto& [name, value] : _response.headers)
		{
			s += name;
			s += ": ";
			s += value;
			s += "\r\n";
			if (name == "Content-Length") hasContentLength = true;
		}

		if (!_response.body.empty() && !hasContentLength)
		{
			s += "Content-Length: ";
			s += std::to_string(_response.body.size());
			s += "\r\n";
		}

		s += "\r\n";
		s += _response.body;
		return s;
	}

	string_view_t Server::DefaultStatusText(const uint16_t _code) noexcept
	{
		switch (_code)
		{
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 503: return "Service Unavailable";
		default: return "Unknown";
		}
	}

	HttpMethod Server::ParseMethod(const string_view_t _method) noexcept
	{
		if (_method == "POST") return HttpMethod::POST;
		if (_method == "PUT") return HttpMethod::PUT;
		if (_method == "DELETE") return HttpMethod::DEL;
		if (_method == "PATCH") return HttpMethod::PATCH;
		if (_method == "HEAD") return HttpMethod::HEAD;
		if (_method == "OPTIONS") return HttpMethod::OPTIONS;
		return HttpMethod::GET;
	}

END_NS
