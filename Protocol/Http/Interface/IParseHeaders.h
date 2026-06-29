//
// Created by nebula on 24. 5. 30.
//

#ifndef IPARSEHEADERS_H
#define IPARSEHEADERS_H

#include <optional>

#include "Http/HttpUtil.h"

BEGIN_NS(ne::protocol::Http)
	class IParseHeader
	{
	public:
		virtual ~IParseHeader() = default;

	public:
		[[nodiscard]]
		constexpr virtual const ResponseData& GetParseData() const noexcept = 0;

	public:
		[[nodiscard]] Method GetMethod() const
		{
			return GetParseData().method;
		}

		[[nodiscard]] string_view_t GetPath() const
		{
			return GetParseData().path;
		}

		[[nodiscard]] string_view_t GetUri() const
		{
			return GetParseData().uri;
		}


		[[nodiscard]] StatusCode GetStatusCode() const
		{
			return GetParseData().status.statusCode;
		}

		[[nodiscard]] string_view_t GetStatusMessage() const
		{
			return GetParseData().status.statusMessage;
		}

		[[nodiscard]] string_view_t GetHttpVersion() const
		{
			return GetParseData().status.httpVersion;
		}

		[[nodiscard]] Status const& GetStatus() const
		{
			return GetParseData().status;
		}

		[[nodiscard]] string_view_t GetHeadersString() const
		{
			return GetParseData().headersString;
		}

		[[nodiscard]] std::span<const Header> GetHeaders() const
		{
			return GetParseData().headers;
		}

		[[nodiscard]] std::optional<Header> GetHeader(const string_view_t _name) const
		{
			if (const auto header = FindHeaderByName(GetParseData().headers, _name))
			{
				return *header;
			}

			return {};
		}

		[[nodiscard]] std::optional<string_view_t> GetHeaderValue(const string_view_t _name) const
		{
			if (const auto header = FindHeaderByName(GetParseData().headers, _name))
			{
				return header->value;
			}

			return {};
		}
	};

END_NS

#endif //IPARSEHEADERS_H
