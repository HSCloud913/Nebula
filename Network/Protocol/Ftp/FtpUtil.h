//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cctype>
#include <charconv>
#include <optional>
#include <string_view>
#include "Network/Protocol/Ftp/FtpBase.h"

BEGIN_NS(ne::network::ftp)

// Parses "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)"
// Returns {host, port} or nullopt on malformed input.
[[nodiscard]] inline std::optional<std::pair<string_t, uint16_t>>
ParsePassiveReply(string_view_t _reply)
{
    const auto lp = _reply.find('(');
    const auto rp = _reply.find(')');
    if (lp == string_view_t::npos || rp == string_view_t::npos || rp <= lp)
        return std::nullopt;

    const auto inner = _reply.substr(lp + 1, rp - lp - 1);
    int parts[6]{};
    int idx = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= inner.size() && idx < 6; ++i) {
        if (i == inner.size() || inner[i] == ',') {
            const auto sub = inner.substr(start, i - start);
            std::from_chars(sub.data(), sub.data() + sub.size(), parts[idx++]);
            start = i + 1;
        }
    }
    if (idx != 6) return std::nullopt;

    string_t host = std::to_string(parts[0]) + '.' +
                    std::to_string(parts[1]) + '.' +
                    std::to_string(parts[2]) + '.' +
                    std::to_string(parts[3]);
    const uint16_t port = static_cast<uint16_t>(parts[4] * 256 + parts[5]);
    return std::make_pair(std::move(host), port);
}

// Parses a single MLSD line: "key=value;key=value; filename"
[[nodiscard]] inline std::optional<FtpEntry> ParseMlsdLine(string_view_t _line)
{
    // Strip trailing \r
    while (!_line.empty() && (_line.back() == '\r' || _line.back() == '\n'))
        _line.remove_suffix(1);

    const auto sp = _line.rfind(' ');
    if (sp == string_view_t::npos) return std::nullopt;

    FtpEntry entry;
    entry.name = string_t(_line.substr(sp + 1));

    const auto facts = _line.substr(0, sp);
    std::size_t pos = 0;
    while (pos < facts.size()) {
        const auto semi = facts.find(';', pos);
        const auto end  = (semi == string_view_t::npos) ? facts.size() : semi;
        const auto fact = facts.substr(pos, end - pos);
        const auto eq   = fact.find('=');
        if (eq != string_view_t::npos) {
            const auto key = fact.substr(0, eq);
            const auto val = fact.substr(eq + 1);
            if (key == "type") {
                entry.isDirectory = (val == "dir" || val == "cdir" || val == "pdir");
            } else if (key == "size") {
                uint64_t sz = 0;
                std::from_chars(val.data(), val.data() + val.size(), sz);
                entry.size = sz;
            }
        }
        pos = (semi == string_view_t::npos) ? facts.size() : semi + 1;
    }
    return entry;
}

// Parses a single Unix LIST line:
// "drwxr-xr-x 2 user group 4096 Jan 01 12:00 dirname"
[[nodiscard]] inline std::optional<FtpEntry> ParseUnixListLine(string_view_t _line)
{
    // Strip trailing \r\n
    while (!_line.empty() && (_line.back() == '\r' || _line.back() == '\n'))
        _line.remove_suffix(1);
    if (_line.size() < 10) return std::nullopt;

    FtpEntry entry;
    entry.isDirectory = (_line[0] == 'd');

    // Tokenise: permissions links user group size date(3) time/year name
    // We want the 5th token (0-indexed: token 4) as size, and everything after token 8 as name.
    int  tok  = 0;
    bool inTok = false;
    std::size_t nameStart = string_view_t::npos;

    for (std::size_t i = 0; i < _line.size(); ++i) {
        const bool space = std::isspace(static_cast<unsigned char>(_line[i]));
        if (!space && !inTok) {
            inTok = true;
            if (tok == 4) {
                uint64_t sz = 0;
                std::from_chars(_line.data() + i, _line.data() + _line.size(), sz);
                entry.size = sz;
            }
            if (tok == 8) { nameStart = i; break; }
        } else if (space && inTok) {
            inTok = false;
            ++tok;
        }
    }
    if (nameStart == string_view_t::npos) return std::nullopt;
    entry.name = string_t(_line.substr(nameStart));
    return entry;
}

END_NS
