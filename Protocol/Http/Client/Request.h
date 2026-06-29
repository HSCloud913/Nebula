//
// Created by nebula on 24. 5. 29.
//

#ifndef HTTPCLIENTREQUEST_H
#define HTTPCLIENTREQUEST_H

#include <future>
#include <chrono>

#include "Response.h"
#include "ConnectionPool.h"

BEGIN_NS(ne::protocol::Http::Client)
	class Request final
	{
		NEBULA_NON_COPYABLE_MOVABLE(Request)

	public:
		Request() = delete;
		~Request() = default;

	private:
		Request(const Method _method, const string_view_t _url, const Protocol _defaultProtocol);

	private:
		Method method;
		Mode mode;
		string_t url;
		UrlElement urlElement;

		string_t headers;
		std::vector<std::byte> body;
		ResponseCallbacks callbacks;
		std::chrono::milliseconds timeout{ 0 };

	public:
		Request& SetMode(const Mode& _mode);
		Request& SetTimeout(std::chrono::milliseconds _timeout);

		Request& AddHeader(const Header& _header);
		Request& AddHeaders(const string_view_t _headersString);
		Request& AddHeaders(const std::initializer_list<const Header> _headers);

		template <IsHeader Header_, std::size_t Extent = std::dynamic_extent>
		Request& AddHeaders(const std::span<const Header_, Extent> _headers);

		template <IsHeader ... Header_>
		Request& AddHeaders(Header_&& ... p_headers);


		Request& SetBody(const string_view_t _body);

		template <IsByte Byte_>
		Request& SetBody(const std::span<const Byte_> _body);

		Request& SetRawCallback(std::function<void_t(ResponseRaw&)> _callback);
		Request& SetHeadersCallback(std::function<void_t(ResponseHeaders&)> _callback);
		Request& SetBodyCallback(std::function<void_t(ResponseBody&)> _callback);
		Request& SetCallback(std::function<void_t(Response&)> _callback);

	public:
		Response Send();
		std::future<Response> SendAsync();

		template <std::size_t bufferSize>
		Response Send();

		template <std::size_t bufferSize>
		std::future<Response> SendAsync();

	private:
		[[nodiscard]] NebulaHttpClientSocket AcquireSocket(const string_t& _connectionKey);
		[[nodiscard]] NebulaHttpClientSocket SendRequest(const string_t& _connectionKey);

	private:
		friend Request Get(string_view_t, Protocol);
		friend Request Post(string_view_t, Protocol);
		friend Request Delete(string_view_t, Protocol);
		friend Request Put(string_view_t, Protocol);
		friend Request MakeRequest(Method, string_view_t, Protocol);
	};

	template <IsHeader Header_, std::size_t Extent>
	Request& Request::AddHeaders(const std::span<const Header_, Extent> _headers)
	{
		auto headersString = string_t{};
		headersString.reserve(_headers.size() * 128);

		for (const auto& header : _headers)
		{
			if (!IsValidHeaderField(header.name) || !IsValidHeaderField(header.value))
			{
				throw ne::Exception("[Request/AddHeaders]", "Header name or value must not contain CR/LF characters");
			}

			headersString += std::format("{}: {}\r\n", header.name, header.value);
		}

		return AddHeaders(headersString);
	}

	template <IsHeader ... Header_>
	Request& Request::AddHeaders(Header_&& ... _headers)
	{
		const auto headers = std::array{ Header(_headers) ... };
		return AddHeaders(std::span{ headers });
	}

	template <IsByte Byte_>
	Request& Request::SetBody(const std::span<const Byte_> _body)
	{
		body.resize(_body.size());
		if constexpr (std::same_as<Byte_, std::byte>)
		{
			std::ranges::copy(_body, body.begin());
		}
		else
		{
			std::ranges::copy(std::span(reinterpret_cast<std::byte const*>(_body.data()), _body.size()), body.begin());
		}

		return *this;
	}

	template <std::size_t bufferSize>
	Response Request::Send()
	{
		const auto connectionKey = ConnectionPool::MakeKey(urlElement.host, urlElement.port, urlElement.protocol == Protocol::HTTPS);
		auto socket = SendRequest(connectionKey);

		return ReceiveResponse<bufferSize>(std::move(socket), std::move(url), std::move(callbacks), connectionKey);
	}

	template <std::size_t bufferSize>
	std::future<Response> Request::SendAsync()
	{
		const auto connectionKey = ConnectionPool::MakeKey(urlElement.host, urlElement.port, urlElement.protocol == Protocol::HTTPS);
		auto socket = SendRequest(connectionKey);

		return std::async(&ReceiveResponse<bufferSize>, std::move(socket), std::move(url), std::move(callbacks), connectionKey);
	}



	[[nodiscard]]
	inline Request Get(const string_view_t _url, const Protocol _defaultProtocol = Protocol::HTTP)
	{
		return Request{ Method::GET, _url, _defaultProtocol };
	}

	[[nodiscard]]
	inline Request Post(const string_view_t _url, const Protocol _defaultProtocol = Protocol::HTTP)
	{
		return Request{ Method::POST, _url, _defaultProtocol };
	}

	[[nodiscard]]
	inline Request Delete(const string_view_t _url, const Protocol _defaultProtocol = Protocol::HTTP)
	{
		return Request{ Method::DEL, _url, _defaultProtocol };
	}

	[[nodiscard]]
	inline Request Put(const string_view_t _url, const Protocol _defaultProtocol = Protocol::HTTP)
	{
		return Request{ Method::PUT, _url, _defaultProtocol };
	}

	[[nodiscard]]
	inline Request MakeRequest(const Method _method, const string_view_t _url, const Protocol _defaultProtocol = Protocol::HTTP)
	{
		return Request{ _method, _url, _defaultProtocol };
	}

END_NS

#endif //HTTPCLIENTREQUEST_H
