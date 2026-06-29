//
// Created by nebula on 24. 6. 17.
//

#ifndef HTTPSERVERRESPONSE_H
#define HTTPSERVERRESPONSE_H

#include <format>

#include "Exception.h"
#include "Http/HttpUtil.h"

BEGIN_NS(ne::protocol::Http::Server)
	class Response
	{
		NEBULA_NON_COPYABLE(Response)

	public:
		NEBULA_DEFAULT_MOVE(Response)

	public:
		Response();
		~Response() = default;

	private:
		Status status;
		string_t headers;
		std::vector<std::byte> body;

	public:
		void_t SetStatusCode(const StatusCode _statusCode);

		void_t AddHeader(const Header& _header);
		void_t AddHeaders(const string_view_t _headersString);
		void_t AddHeaders(const std::initializer_list<const Header> _headers);

		template <IsHeader Header_, std::size_t Extent = std::dynamic_extent>
		void_t AddHeaders(const std::span<const Header_, Extent> _headers);

		template <IsHeader ... Header_>
		void_t AddHeaders(Header_&& ... p_headers);


		void_t SetBody(const string_view_t _body);

		template <IsByte Byte_>
		void_t SetBody(const std::span<const Byte_> _body);

	public:
		std::vector<std::byte> GetResponseString(const Mode _mode = Mode::Identify);
	};


	template <IsHeader Header_, std::size_t Extent>
	void_t Response::AddHeaders(const std::span<const Header_, Extent> _headers)
	{
		auto headersString = string_t{};
		headersString.reserve(_headers.size() * 128);

		for (const auto& header : _headers)
		{
			if (!IsValidHeaderField(header.name) || !IsValidHeaderField(header.value))
			{
				throw ne::Exception("[Response/AddHeaders]", "Header name or value must not contain CR/LF characters");
			}

			headersString += std::format("{}: {}\r\n", header.name, header.value);
		}

		AddHeaders(headersString);
	}

	template <IsHeader ... Header_>
	void_t Response::AddHeaders(Header_&& ... _headers)
	{
		const auto headers = std::array{ Header(_headers) ... };
		AddHeaders(std::span{ headers });
	}

	template <IsByte Byte_>
	void_t Response::SetBody(const std::span<const Byte_> _body)
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
	}

END_NS

#endif //HTTPSERVERRESPONSE_H
