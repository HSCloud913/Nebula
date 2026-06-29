//
// Created by hscloud on 25. 6. 29.
//

#include "../Http1Client.h"
#include "Stream/PlainStream.h"
#include "Socket/Socket.h"
#include <array>
#include <span>
#include <string>

BEGIN_NS(ne::network::http_1)
	Client::Client(IIoEngine& _engine) noexcept
		: engine(_engine) {}



	ne::Task<ne::Result<HttpResponse, ne::OsError>> Client::Get(const string_view_t _host, const uint16_t _port, const string_view_t _path, HttpHeaders _headers) const
	{
		HttpRequest request;
		request.method = HttpMethod::GET;
		request.path = string_t(_path);
		request.headers = std::move(_headers);

		co_return co_await Execute(_host, _port, std::move(request));
	}

	ne::Task<ne::Result<HttpResponse, ne::OsError>> Client::Post(const string_view_t _host, const uint16_t _port, const string_view_t _path, const string_view_t _body, HttpHeaders _headers) const
	{
		HttpRequest request;
		request.method = HttpMethod::POST;
		request.path = string_t(_path);
		request.body = string_t(_body);
		request.headers = std::move(_headers);

		co_return co_await Execute(_host, _port, std::move(request));
	}



	ne::Task<ne::Result<HttpResponse, ne::OsError>> Client::Execute(string_view_t _host, uint16_t _port, HttpRequest _request) const
	{
		// Create + connect socket (synchronous connect, async send/recv)
		auto sockRes = Socket::CreateTcp();
		if (sockRes.IsError())
		{
			auto err = std::move(sockRes.Error());
			err.Context("[Http1Client/Execute]");
			co_return ne::Result<HttpResponse, ne::OsError>::Error(std::move(err));
		}

		Socket sock = std::move(sockRes.Value());

		if (auto r = sock.Connect(_host, _port); r.IsError())
		{
			auto err = std::move(r.Error());
			err.Context("[Http1Client/Execute]");
			co_return ne::Result<HttpResponse, ne::OsError>::Error(std::move(err));
		}

		if (auto r = sock.SetNonBlocking(true); r.IsError())
		{
			auto err = std::move(r.Error());
			err.Context("[Http1Client/Execute]");
			co_return ne::Result<HttpResponse, ne::OsError>::Error(std::move(err));
		}

		auto streamRes = PlainStream::Create(std::move(sock), engine);
		if (streamRes.IsError())
		{
			auto err = std::move(streamRes.Error());
			err.Context("[Http1Client/Execute]");
			co_return ne::Result<HttpResponse, ne::OsError>::Error(std::move(err));
		}

		PlainStream& stream = streamRes.Value();

		// Serialize and send
		const string_t reqStr = Serialize(_request, _host);
		const auto sendSpan = std::span{
			reinterpret_cast<const byte_t*>(reqStr.data()),
			reqStr.size()
		};

		if (auto r = co_await stream.Send(sendSpan); r.IsError())
		{
			auto err = std::move(r.Error());
			err.Context("[Http1Client/Execute]");
			co_return ne::Result<HttpResponse, ne::OsError>::Error(std::move(err));
		}

		// Receive response — Connection: close 이므로 EOF 까지 읽음
		string_t raw;
		raw.reserve(4096);
		std::array<byte_t, 4096> buf{};

		while (true)
		{
			auto r = co_await stream.Receive(std::span{ buf });
			if (r.IsError())
			{
				auto err = std::move(r.Error());
				err.Context("[Http1Client/Execute]");
				co_return ne::Result<HttpResponse, ne::OsError>::Error(std::move(err));
			}

			const auto n = r.Value();
			if (n == 0) break; // EOF

			raw.append(reinterpret_cast<const char*>(buf.data()), n);
		}

		co_return ParseResponse(raw);
	}



	string_t Client::Serialize(const HttpRequest& _request, const string_view_t _host)
	{
		static constexpr string_view_t Methods[] = {
			"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"
		};

		string_t s;
		s.reserve(256);
		s += Methods[static_cast<std::size_t>(_request.method)];
		s += ' ';
		s += _request.path;
		s += " HTTP/1.1\r\nHost: ";
		s += _host;
		s += "\r\nConnection: close\r\n";

		for (const auto& [name, value] : _request.headers)
		{
			s += name;
			s += ": ";
			s += value;
			s += "\r\n";
		}

		if (!_request.body.empty())
		{
			s += "Content-Length: ";
			s += std::to_string(_request.body.size());
			s += "\r\n";
		}

		s += "\r\n";
		s += _request.body;

		return s;
	}

	ne::Result<HttpResponse, ne::OsError> Client::ParseResponse(string_view_t _raw)
	{
		const auto sep = _raw.find("\r\n\r\n");
		if (sep == string_view_t::npos)
			return ne::Result<HttpResponse, ne::OsError>::Error(
				ne::OsError{ 0, "incomplete HTTP response" }
			);

		HttpResponse response;

		// Status line: "HTTP/1.1 200 OK"
		const auto lineEnd = _raw.find("\r\n");
		const auto statusLine = _raw.substr(0, lineEnd);

		const auto sp1 = statusLine.find(' ');
		if (sp1 == string_view_t::npos) return ne::Result<HttpResponse, ne::OsError>::Error(ne::OsError{ 0, "malformed status line" });

		const auto sp2 = statusLine.find(' ', sp1 + 1);
		const auto codeView = (sp2 != string_view_t::npos)
								? statusLine.substr(sp1 + 1, sp2 - sp1 - 1)
								: statusLine.substr(sp1 + 1);

		// stoi 미사용 (-fno-exceptions): 직접 파싱
		uint16_t code = 0;
		for (const char c : codeView)
		{
			if (c < '0' || c > '9') return ne::Result<HttpResponse, ne::OsError>::Error(ne::OsError{ 0, "invalid status code" });
			code = static_cast<uint16_t>(code * 10 + static_cast<uint16_t>(c - '0'));
		}
		response.status = code;

		if (sp2 != string_view_t::npos) response.statusText = string_t(statusLine.substr(sp2 + 1));

		// Headers: "Name: value\r\n" 반복
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

				response.headers.emplace_back(std::move(name), std::move(value));
			}
			pos = end + 2;
		}

		response.body = string_t(_raw.substr(sep + 4));
		return ne::Result<HttpResponse, ne::OsError>::Ok(std::move(response));
	}

END_NS
