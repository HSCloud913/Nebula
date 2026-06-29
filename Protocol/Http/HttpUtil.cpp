//
// Created by nebula on 24. 5. 30.
//

#include "HttpUtil.h"

BEGIN_NS(ne::protocol::Http)
	inline HostAndPort SplitDomain(const string_view_t _domain)
	{
		if (const auto pos = _domain.rfind(':'); pos != string_view_t::npos)
		{
			if (const auto port = StringToInt<int_t>(_domain.substr(pos + 1)))
			{
				return HostAndPort{ .host{ _domain.substr(0, pos) }, .port{ port } };
			}
			else
			{
				return HostAndPort{ .host{ _domain.substr(0, pos) }, .port = std::nullopt };
			}
		}

		return HostAndPort{ .host{ _domain }, .port = std::nullopt };
	}



	UrlElement SplitUrl(const string_view_t _url)
	{
		using namespace std::string_view_literals;

		if (_url.empty()) return {};

		auto result = UrlElement{};
		constexpr auto Whitespace = " \t\r\n"sv;

		auto startPos = _url.find_first_not_of(Whitespace);
		if (startPos == string_view_t::npos) return {};

		constexpr auto Suffix = "://"sv;

		if (const auto pos = _url.find(Suffix, startPos); pos != string_view_t::npos)
		{
			auto protocolString = _url.substr(startPos, pos - startPos);
			if (StringFormat::EqualCaseInsensitive(protocolString, "http"))
			{
				result.protocol = Protocol::HTTP;
			}
			else if (StringFormat::EqualCaseInsensitive(protocolString, "https"))
			{
				result.protocol = Protocol::HTTPS;
			}
			else
			{
				result.protocol = Protocol::UNKNOWN;
			}
			result.port = static_cast<int_t>(result.protocol);

			startPos = pos + Suffix.length();
		}

		if (const auto slashPos = _url.find('/', startPos); slashPos != string_view_t::npos)
		{
			auto [host, port] = SplitDomain(_url.substr(startPos, slashPos - startPos));

			result.host = host;
			if (port) result.port = *port;

			startPos = slashPos;
		}
		else
		{
			auto [host, port] = SplitDomain(_url.substr(startPos));

			result.host = host;
			result.path = "/"sv;
			if (port) result.port = *port;

			return result;
		}

		const auto endPos = _url.find_last_not_of(Whitespace) + 1;
		result.path = _url.substr(startPos, endPos - startPos);

		return result;
	}

END_NS
