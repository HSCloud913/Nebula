//
// Created by nebula on 24. 5. 29.
//

#include "Request.h"
#include <sstream>



BEGIN_NS(ne::protocol::Http::Client)
	[[nodiscard]]
	constexpr bool_t IsUriCharacter(const char_t _character) noexcept
	{
		constexpr auto characters = string_view_t("%-._~:/?#[]@!$&'()*+,;=");

		return (_character >= '0' && _character <= '9') ||
				(_character >= 'a' && _character <= 'z') ||
				(_character >= 'A' && _character <= 'Z') ||
				characters.find(_character) != string_view_t::npos;
	}

	[[nodiscard]]
	inline string_t UriEncode(const string_view_t _uri)
	{
		auto result = string_t();
		result.reserve(_uri.size());

		for (const auto character : _uri)
		{
			if (IsUriCharacter(character))
			{
				result += character;
			}
			else
			{
				result += std::format("%{:02X}", static_cast<byte_t>(character));
			}
		}

		return result;
	}

	[[nodiscard]]
	inline string_view_t MethodToString(const Method _method)
	{
		using enum Method;
		switch (_method)
		{
		case CONNECT: return "CONNECT";
		case DEL: return "DELETE";
		case GET: return "GET";
		case HEAD: return "HEAD";
		case OPTIONS: return "OPTIONS";
		case PATCH: return "PATCH";
		case POST: return "POST";
		case PUT: return "PUT";
		case TRACE: return "TRACE";
		}

		return "";
	}



	/*--------------------------------------------------*/



	Request::Request(const Method _method, const string_view_t _url, const Protocol _defaultProtocol)
		: method(_method)
		, mode(Mode::Identify)
		, url(UriEncode(_url))
		, urlElement(SplitUrl(string_view_t(url)))
		, headers("\r\n")
	{
		if (urlElement.protocol == Protocol::UNKNOWN)
		{
			urlElement.protocol = _defaultProtocol;
		}
		if (urlElement.port == static_cast<int_t>(Protocol::UNKNOWN))
		{
			urlElement.port = static_cast<int_t>(urlElement.protocol);
		}
	}



	Request& Request::SetMode(const Mode& _mode)
	{
		mode = _mode;

		return *this;
	}

	Request& Request::SetTimeout(const std::chrono::milliseconds _timeout)
	{
		timeout = _timeout;

		return *this;
	}


	Request& Request::AddHeader(const Header& _header)
	{
		if (!IsValidHeaderField(_header.name) || !IsValidHeaderField(_header.value))
		{
			throw ne::Exception("[Request/AddHeader]", "Header name or value must not contain CR/LF characters");
		}

		return AddHeaders(std::format("{}: {}", _header.name, _header.value));
	}

	Request& Request::AddHeaders(const string_view_t _headersString)
	{
		if (_headersString.empty()) return *this;

		headers += _headersString;
		if (_headersString.back() != '\n') headers += "\r\n";

		return *this;
	}

	Request& Request::AddHeaders(const std::initializer_list<const Header> _headers)
	{
		return AddHeaders(std::span(_headers));
	}


	Request& Request::SetBody(const string_view_t _body)
	{
		return SetBody(StringToData<std::byte>(_body));
	}


	Request& Request::SetRawCallback(std::function<void_t(ResponseRaw&)> _callback)
	{
		callbacks.handleRaw = std::move(_callback);
		return *this;
	}

	Request& Request::SetHeadersCallback(std::function<void_t(ResponseHeaders&)> _callback)
	{
		callbacks.handleHeaders = std::move(_callback);
		return *this;
	}

	Request& Request::SetBodyCallback(std::function<void_t(ResponseBody&)> _callback)
	{
		callbacks.handleBody = std::move(_callback);
		return *this;
	}

	Request& Request::SetCallback(std::function<void_t(Response&)> _callback)
	{
		callbacks.handle = std::move(_callback);
		return *this;
	}



	Response Request::Send()
	{
		const auto connectionKey = ConnectionPool::MakeKey(urlElement.host, urlElement.port, urlElement.protocol == Protocol::HTTPS);
		auto socket = SendRequest(connectionKey);

		return ReceiveResponse<>(std::move(socket), std::move(url), std::move(callbacks), connectionKey);
	}

	std::future<Response> Request::SendAsync()
	{
		const auto connectionKey = ConnectionPool::MakeKey(urlElement.host, urlElement.port, urlElement.protocol == Protocol::HTTPS);
		auto socket = SendRequest(connectionKey);

		return std::async(&ReceiveResponse<>, std::move(socket), std::move(url), std::move(callbacks), connectionKey);
	}



	NebulaHttpClientSocket Request::AcquireSocket(const string_t& _connectionKey)
	{
		if (auto pooled = ConnectionPool::GetInstance().Acquire(_connectionKey))
		{
			pooled->SetTimeout(timeout);
			return std::move(*pooled);
		}

		auto socket = OpenSocket(urlElement.host, urlElement.port, urlElement.protocol == Protocol::HTTPS);
		socket.SetTimeout(timeout);
		socket.Connect();

		return socket;
	}

	NebulaHttpClientSocket Request::SendRequest(const string_t& _connectionKey)
	{
		using namespace std::string_view_literals;

		if (!body.empty())
		{
			if (mode == Mode::Identify)
			{
				headers += std::format("Content-Length: {}\r\n", body.size());
			}
			else if (mode == Mode::Chunked)
			{
				headers += "Transfer-Encoding: chunked\r\n";
			}
		}

		auto socket = AcquireSocket(_connectionKey);

		if (mode == Mode::Identify)
		{
			const auto data = ConcatenateByteData(
				MethodToString(method),
				' ',
				urlElement.path,
				" HTTP/1.1\r\nHost: "sv,
				urlElement.host,
				headers,
				"\r\n"sv,
				body
			);
			socket.Write(data);
		}
		else if (mode == Mode::Chunked)
		{
			std::stringstream ss;
			ss << std::hex << body.size();

			const auto data = ConcatenateByteData(
				MethodToString(method),
				' ',
				urlElement.path,
				" HTTP/1.1\r\nHost: "sv,
				urlElement.host,
				headers,
				"\r\n"sv,
				ss.str(),
				"\r\n"sv,
				body,
				"\r\n0\r\n\r\n"sv
			);
			socket.Write(data);
		}
		else
		{
			throw ne::Exception("[Request/SendRequest]", "Invalid transport mode");
		}

		return socket;
	}

END_NS
