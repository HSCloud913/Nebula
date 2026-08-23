//
// Created by hscloud on 26. 7. 29.
//

#include "Network/Protocol/Http/Message/Params.h"



namespace ne::network::http
{
	namespace
	{
		[[nodiscard]] int_t HexValue(const char _char) noexcept
		{
			if (_char >= '0' && _char <= '9') return _char - '0';
			if (_char >= 'a' && _char <= 'f') return _char - 'a' + 10;
			if (_char >= 'A' && _char <= 'F') return _char - 'A' + 10;

			return -1;
		}
	}



	string_t UrlDecode(const string_view_t _text, const bool_t _isPlusAsSpace)
	{
		string_t result;
		result.reserve(_text.size());

		for (std::size_t i = 0; i < _text.size(); ++i)
		{
			const char current = _text[i];

			if (current == '%' && i + 2 < _text.size())
			{
				const int_t high = HexValue(_text[i + 1]);
				const int_t low = HexValue(_text[i + 2]);
				if (high >= 0 && low >= 0)
				{
					result += static_cast<char>((high << 4) | low);
					i += 2;
					continue;
				}
			}

			result += (_isPlusAsSpace && current == '+') ? ' ' : current;
		}

		return result;
	}



	QueryParams QueryParams::Parse(const string_view_t _target)
	{
		QueryParams result;

		const auto question = _target.find('?');
		if (question == string_view_t::npos) return result;

		string_view_t query = _target.substr(question + 1);
		while (!query.empty())
		{
			const auto ampersand = query.find('&');
			const string_view_t pair = query.substr(0, ampersand);
			query = (ampersand == string_view_t::npos) ? string_view_t{} : query.substr(ampersand + 1);

			if (pair.empty()) continue;

			const auto equal = pair.find('=');
			const string_view_t name = (equal == string_view_t::npos) ? pair : pair.substr(0, equal);
			const string_view_t value = (equal == string_view_t::npos) ? string_view_t{} : pair.substr(equal + 1);

			result.params.emplace_back(UrlDecode(name, true), UrlDecode(value, true));
		}

		return result;
	}
}
