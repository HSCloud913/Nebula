//
// Created by nebula on 24. 5. 29.
//

#ifndef FTPUTIL_H
#define FTPUTIL_H

#include <charconv>
#include <format>
#include <optional>
#include <vector>

#include "FtpBase.h"
#include "Ascii.h"
#include "StringFormat.h"

BEGIN_NS(ne::protocol::Ftp)
	[[nodiscard]]
	inline std::optional<ulonglong_t> ParseUnsigned(const string_view_t _text)
	{
		auto value = ulonglong_t{};
		if (std::from_chars(_text.data(), _text.data() + _text.size(), value).ec == std::errc{}) return value;

		return std::nullopt;
	}

	[[nodiscard]]
	inline std::optional<int_t> ParseReplyCode(const string_view_t _line)
	{
		if (_line.size() < 3) return std::nullopt;

		auto value = int_t{};
		if (std::from_chars(_line.data(), _line.data() + 3, value).ec != std::errc{}) return std::nullopt;

		return value;
	}

	[[nodiscard]]
	inline bool_t IsFinalReplyLine(const string_view_t _line, const int_t _code)
	{
		return _line.starts_with(std::format("{:03} ", _code));
	}

	[[nodiscard]]
	inline std::optional<PassiveAddress> ParsePassiveReply(const string_view_t _message)
	{
		const auto open = _message.find('(');
		if (open == string_view_t::npos) return std::nullopt;

		const auto close = _message.find(')', open);
		if (close == string_view_t::npos) return std::nullopt;

		auto tokens = std::vector<string_t>();
		StringFormat::Tokenize(string_t(_message.substr(open + 1, close - open - 1)), string_t(","), tokens);
		if (tokens.size() != 6) return std::nullopt;

		for (const auto& token : tokens)
		{
			if (token.empty() || !std::ranges::all_of(token, [](const char_t _c) { return _c >= '0' && _c <= '9'; })) return std::nullopt;
		}

		const auto p1 = ParseUnsigned(tokens[4]);
		const auto p2 = ParseUnsigned(tokens[5]);
		if (!p1 || !p2) return std::nullopt;

		return PassiveAddress
		{
			.host = std::format("{}.{}.{}.{}", tokens[0], tokens[1], tokens[2], tokens[3]),
			.port = static_cast<int_t>(*p1 * 256 + *p2)
		};
	}

	[[nodiscard]]
	inline std::optional<Entry> ParseMlsdLine(const string_view_t _line)
	{
		const auto line = StringFormat::Trim(string_t(_line));

		const auto semicolonPos = line.rfind(';');
		if (semicolonPos == string_t::npos || semicolonPos + 1 >= line.size() || line[semicolonPos + 1] != ' ') return std::nullopt;

		auto name = line.substr(semicolonPos + 2);
		if (name.empty()) return std::nullopt;

		auto facts = std::vector<string_t>();
		StringFormat::Tokenize(line.substr(0, semicolonPos), string_t(";"), facts);

		auto entry = Entry{ .name = std::move(name) };
		for (const auto& fact : facts)
		{
			const auto equalsPos = fact.find('=');
			if (equalsPos == string_t::npos) continue;

			const auto key = StringFormat::Lower(fact.substr(0, equalsPos));
			const auto value = fact.substr(equalsPos + 1);

			if (key == "size")
			{
				entry.size = ParseUnsigned(value).value_or(0);
			}
			else if (key == "type")
			{
				const auto type = StringFormat::Lower(value);
				entry.isDirectory = (type == "dir" || type == "cdir" || type == "pdir");
			}
		}

		return entry;
	}

	[[nodiscard]]
	inline std::optional<Entry> ParseUnixListLine(const string_view_t _line)
	{
		const auto line = StringFormat::Trim(string_t(_line));
		if (line.empty()) return std::nullopt;

		auto tokens = std::vector<string_t>();
		auto pos = std::size_t{};

		for (auto i = 0; i < 8; ++i)
		{
			while (pos < line.size() && Ascii::IsSpace(line[pos])) ++pos;
			const auto start = pos;
			while (pos < line.size() && !Ascii::IsSpace(line[pos])) ++pos;
			if (start == pos) return std::nullopt;

			tokens.push_back(line.substr(start, pos - start));
		}
		while (pos < line.size() && Ascii::IsSpace(line[pos])) ++pos;
		if (pos >= line.size()) return std::nullopt;

		auto name = line.substr(pos);
		if (const auto arrow = name.find(" -> "); arrow != string_t::npos) name = name.substr(0, arrow);

		auto entry = Entry{ .name = std::move(name) };
		entry.isDirectory = !tokens[0].empty() && tokens[0][0] == 'd';
		entry.size = ParseUnsigned(tokens[4]).value_or(0);

		return entry;
	}

END_NS

#endif //FTPUTIL_H
