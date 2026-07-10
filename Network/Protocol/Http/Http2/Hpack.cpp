//
// Created by hscloud on 25. 6. 29.
//

#include "Network/Protocol/Http/Http2/Hpack.h"
#include <array>
#include <cstring>

BEGIN_NS (ne::network::http_2)

// ── Static Table (RFC 7541 Appendix A) ───────────────────────────────────────
struct StaticEntry
{
	ne::string_view_t name, value;
};

static constexpr StaticEntry kStatic[] = { { ":authority", "" },                    // 1
											{ ":method", "GET" },                   // 2
											{ ":method", "POST" },                  // 3
											{ ":path", "/" },                       // 4
											{ ":path", "/index.html" },             // 5
											{ ":scheme", "http" },                  // 6
											{ ":scheme", "https" },                 // 7
											{ ":status", "200" },                   // 8
											{ ":status", "204" },                   // 9
											{ ":status", "206" },                   // 10
											{ ":status", "304" },                   // 11
											{ ":status", "400" },                   // 12
											{ ":status", "404" },                   // 13
											{ ":status", "500" },                   // 14
											{ "accept-charset", "" },               // 15
											{ "accept-encoding", "gzip, deflate" }, // 16
											{ "accept-language", "" },              // 17
											{ "accept-ranges", "" },                // 18
											{ "accept", "" },                       // 19
											{ "access-control-allow-origin", "" },  // 20
											{ "age", "" },                          // 21
											{ "allow", "" },                        // 22
											{ "authorization", "" },                // 23
											{ "cache-control", "" },                // 24
											{ "content-disposition", "" },          // 25
											{ "content-encoding", "" },             // 26
											{ "content-language", "" },             // 27
											{ "content-length", "" },               // 28
											{ "content-location", "" },             // 29
											{ "content-range", "" },                // 30
											{ "content-type", "" },                 // 31
											{ "cookie", "" },                       // 32
											{ "date", "" },                         // 33
											{ "etag", "" },                         // 34
											{ "expect", "" },                       // 35
											{ "expires", "" },                      // 36
											{ "from", "" },                         // 37
											{ "host", "" },                         // 38
											{ "if-match", "" },                     // 39
											{ "if-modified-since", "" },            // 40
											{ "if-none-match", "" },                // 41
											{ "if-range", "" },                     // 42
											{ "if-unmodified-since", "" },          // 43
											{ "last-modified", "" },                // 44
											{ "link", "" },                         // 45
											{ "location", "" },                     // 46
											{ "max-forwards", "" },                 // 47
											{ "proxy-authenticate", "" },           // 48
											{ "proxy-authorization", "" },          // 49
											{ "range", "" },                        // 50
											{ "referer", "" },                      // 51
											{ "refresh", "" },                      // 52
											{ "retry-after", "" },                  // 53
											{ "server", "" },                       // 54
											{ "set-cookie", "" },                   // 55
											{ "strict-transport-security", "" },    // 56
											{ "transfer-encoding", "" },            // 57
											{ "user-agent", "" },                   // 58
											{ "vary", "" },                         // 59
											{ "via", "" },                          // 60
											{ "www-authenticate", "" },             // 61
};
static constexpr std::size_t kStaticSize = std::size(kStatic);

// ── Huffman Table (RFC 7541 Appendix B) ──────────────────────────────────────
// kHuffCode[sym] = code value (MSB-aligned within length)
// kHuffLen[sym]  = code bit length
static constexpr uint32_t kHuffCode[256] = {
	/*   0 */ 0x1ff8, 0x7fffd8, 0xfffffe2, 0xfffffe3, 0xfffffe4, 0xfffffe5,
			/*   6 */ 0xfffffe6, 0xfffffe7, 0xfffffe8, 0xffffea, 0x3ffffffc, 0xfffffe9,
			/*  12 */ 0xfffffea, 0x3ffffffd, 0xfffffeb, 0xfffffec, 0xfffffed, 0xfffffee,
			/*  18 */ 0xfffffef, 0xffffff0, 0xffffff1, 0xffffff2, 0x3ffffffe, 0xffffff3,
			/*  24 */ 0xffffff4, 0xffffff5, 0xffffff6, 0xffffff7, 0xffffff8, 0xffffff9,
			/*  30 */ 0xffffffa, 0xffffffb,
			/*  32 */ 0x14, 0x3f8, 0x3f9, 0xffa, 0x1ff9, 0x15,
			/*  38 */ 0xf8, 0x7fa, 0x3fa, 0x3fb, 0xf9, 0x7fb,
			/*  44 */ 0xfa, 0x16, 0x17, 0x18,
			/*  48 */ 0x0, 0x1, 0x2, 0x19, 0x1a, 0x1b,
			/*  54 */ 0x1c, 0x1d, 0x1e, 0x1f,
			/*  58 */ 0x5c, 0xfb, 0x7ffc, 0x20, 0xffb, 0x3fc,
			/*  64 */ 0x1ffa, 0x21, 0x5d, 0x5e, 0x5f, 0x60,
			/*  70 */ 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
			/*  76 */ 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c,
			/*  82 */ 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72,
			/*  88 */ 0xfc, 0x73, 0xfd, 0x1ffb, 0x7fff0, 0x1ffc,
			/*  94 */ 0x3ffc, 0x22, 0x7ffd,
			/*  97 */ 0x3, 0x23, 0x4, 0x24, 0x5, 0x25,
			/* 103 */ 0x26, 0x27, 0x6, 0x74, 0x75, 0x28,
			/* 109 */ 0x29, 0x2a, 0x7, 0x2b, 0x76, 0x8,
			/* 115 */ 0x9, 0xa, 0x2c, 0x77, 0x78, 0x79,
			/* 121 */ 0x7a, 0x2d, 0x7ffe, 0x7fc, 0x3ffd, 0x1ffd,
			/* 127 */ 0xffffffc,
			/* 128 */ 0xfffe6, 0x3fffd2, 0xfffe7, 0xfffe8, 0x3fffd3, 0x3fffd4,
			/* 134 */ 0x3fffd5, 0x7fffd9, 0x3fffd6, 0x7fffda, 0x7fffdb, 0x7fffdc,
			/* 140 */ 0x7fffdd, 0x7fffde, 0xffffeb, 0x7fffdf, 0xffffec, 0xffffed,
			/* 146 */ 0x3fffd7, 0x7fffe0, 0xffffee, 0x7fffe1, 0x7fffe2, 0x7fffe3,
			/* 152 */ 0x7fffe4, 0x1fffdc, 0x3fffd8, 0x7fffe5, 0x3fffd9, 0x7fffe6,
			/* 158 */ 0x7fffe7, 0xffffef, 0x3fffda, 0x1fffdd, 0xfffe9, 0x3fffdb,
			/* 164 */ 0x3fffdc, 0x7fffe8, 0x7fffe9, 0x1fffde, 0x7fffea, 0x3fffdd,
			/* 170 */ 0x3fffde, 0xfffff0, 0x1fffdf, 0x3fffdf, 0x7fffeb, 0x7fffec,
			/* 176 */ 0x1fffe0, 0x1fffe1, 0x3fffe0, 0x1fffe2, 0x7fffed, 0x3fffe1,
			/* 182 */ 0x7fffee, 0x7fffef, 0xfffea, 0x3fffe2, 0x3fffe3, 0x3fffe4,
			/* 188 */ 0x7ffff0, 0x3fffe5, 0x3fffe6, 0x7ffff1, 0x3ffffe0, 0x3ffffe1,
			/* 194 */ 0xfffeb, 0x7fff1, 0x3ffffe2, 0x3ffffe3, 0x3ffffe4, 0x7ffffde,
			/* 200 */ 0x7ffffdf, 0x3ffffe5, 0xfffff1, 0x1ffffec, 0x7fff2, 0x1fffe3,
			/* 206 */ 0x3ffffe6, 0x7ffffe0, 0x7ffffe1, 0x3ffffe7, 0x7ffffe2, 0xfffff2,
			/* 212 */ 0x1fffe4, 0x1fffe5, 0x3ffffe8, 0x3ffffe9, 0xffffffd, 0x7ffffe3,
			/* 218 */ 0x7ffffe4, 0x7ffffe5, 0xfffec, 0xfffff3, 0xfffed, 0x1fffe6,
			/* 224 */ 0x3fffe7, 0x1fffe7, 0x1fffe8, 0x7ffff2, 0x3fffe8, 0x1ffffed,
			/* 230 */ 0x7fff3, 0x3ffffea, 0x3ffffeb, 0x1ffffee, 0x1ffffef, 0xfffff4,
			/* 236 */ 0xfffff5, 0x3ffffec, 0x7ffff3, 0x3ffffed, 0x7ffff4, 0x3ffffee,
			/* 242 */ 0x3ffffef, 0xfffff6, 0x7ffff5, 0xfffff7, 0xfffff8, 0xfffff9,
			/* 248 */ 0xfffffa, 0xfffffb, 0xfffffc, 0xfffffd, 0xfffffe0, 0xfffffe1,
			/* 254 */ 0xfffffe2, 0xfffffe3,
};
static constexpr uint8_t kHuffLen[256] = {
	/*   0 */ 13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28,
			/*  16 */ 28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 28,
			/*  32 */ 6, 10, 10, 12, 13, 6, 8, 11, 10, 10, 8, 11, 8, 6, 6, 6,
			/*  48 */ 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 7, 8, 15, 6, 12, 10,
			/*  64 */ 13, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			/*  80 */ 7, 7, 7, 7, 7, 7, 7, 7, 8, 7, 8, 13, 19, 13, 14, 6,
			/*  96 */ 15, 5, 6, 5, 6, 5, 6, 6, 6, 5, 7, 7, 6, 6, 6, 5,
			/* 112 */ 6, 7, 5, 5, 5, 6, 7, 7, 7, 7, 6, 15, 11, 14, 13, 28,
			/* 128 */ 20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23,
			/* 144 */ 24, 24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24,
			/* 160 */ 22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22, 24, 21, 22, 23, 23,
			/* 176 */ 21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23,
			/* 192 */ 26, 26, 20, 19, 26, 26, 26, 27, 27, 26, 24, 25, 19, 21, 26, 27,
			/* 208 */ 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27, 20, 24, 20, 21,
			/* 224 */ 22, 21, 21, 23, 22, 25, 19, 26, 26, 25, 25, 24, 24, 26, 23, 26,
			/* 240 */ 23, 26, 26, 24, 23, 24, 24, 24, 24, 24, 24, 24, 28, 28, 28, 28,
};

// ── Integer helpers ───────────────────────────────────────────────────────────
static void EncodeInt(std::vector<ne::byte_t>& _out, uint32_t _val, uint8_t _prefixBits, uint8_t _firstByte)
{
	const uint32_t maxVal = (1u << _prefixBits) - 1u;
	if (_val < maxVal)
	{
		_out.push_back(ne::byte_t(_firstByte | uint8_t(_val)));
		return;
	}
	_out.push_back(ne::byte_t(_firstByte | uint8_t(maxVal)));
	_val -= maxVal;
	while (_val >= 128u)
	{
		_out.push_back(ne::byte_t((_val & 0x7Fu) | 0x80u));
		_val >>= 7;
	}
	_out.push_back(ne::byte_t(_val));
}

static bool ParseInt(const ne::byte_t* _data, std::size_t _len, std::size_t& _i, uint8_t _prefixBits, uint32_t& _out)
{
	if (_i >= _len) return false;
	const uint32_t mask = (1u << _prefixBits) - 1u;
	_out = uint32_t(_data[_i] & uint8_t(mask));
	++_i;
	if (_out < mask) return true;
	uint32_t m = 0;
	while (_i < _len)
	{
		const uint8_t b = _data[_i++];
		_out += uint32_t(b & 0x7Fu) << m;
		m += 7;
		if (!(b & 0x80u)) return true;
	}
	return false;
}

// ── Huffman decode ────────────────────────────────────────────────────────────
static bool HuffDecode(const ne::byte_t* _in, std::size_t _inLen, ne::string_t& _out)
{
	_out.clear();
	uint32_t buf = 0;
	int bits = 0;
	std::size_t i = 0;

	while (i < _inLen || bits > 0)
	{
		while (bits < 30 && i < _inLen)
		{
			buf = (buf << 8) | uint32_t(_in[i++]);
			bits += 8;
		}
		if (bits <= 0) break;

		bool found = false;
		for (int s = 0; s < 256; ++s)
		{
			const int len = kHuffLen[s];
			if (len > bits) continue;
			const uint32_t code = kHuffCode[s];
			if ((buf >> (bits - len)) == code)
			{
				_out += char(s);
				buf &= (len < 32) ? ((1u << (bits - len)) - 1u) : 0u;
				bits -= len;
				found = true;
				break;
			}
		}
		if (!found)
		{
			if (i >= _inLen && bits <= 7) break; // EOS padding
			return false;
		}
	}
	return true;
}

// ── String helpers ────────────────────────────────────────────────────────────
static void EncodeStr(std::vector<ne::byte_t>& _out, ne::string_view_t _str)
{
	EncodeInt(_out, uint32_t(_str.size()), 7, 0x00u); // H=0, literal
	for (char c : _str) _out.push_back(ne::byte_t(c));
}

static bool ParseStr(const ne::byte_t* _data, std::size_t _len, std::size_t& _i, ne::string_t& _out)
{
	if (_i >= _len) return false;
	const bool huffman = (_data[_i] & 0x80u) != 0;
	uint32_t strLen;
	if (!ParseInt(_data, _len, _i, 7, strLen)) return false;
	if (_i + strLen > _len) return false;
	if (huffman) { if (!HuffDecode(_data + _i, strLen, _out)) _out.clear(); }
	else { _out.assign(reinterpret_cast<const char*>(_data + _i), strLen); }
	_i += strLen;
	return true;
}

// ── Public API ────────────────────────────────────────────────────────────────
namespace Hpack
{
	std::vector<ne::byte_t> Encode(ne::string_view_t _method, ne::string_view_t _path, ne::string_view_t _scheme, ne::string_view_t _authority, const ne::network::HttpHeaders& _headers)
	{
		std::vector<ne::byte_t> out;
		out.reserve(128);

		// :method — indexed if GET(2) or POST(3)
		if (_method == "GET") out.push_back(0x82u);
		else if (_method == "POST") out.push_back(0x83u);
		else
		{
			out.push_back(0x40u);
			EncodeStr(out, ":method");
			EncodeStr(out, _method);
		}

		// :path — indexed if "/" (4)
		if (_path == "/") out.push_back(0x84u);
		else
		{
			out.push_back(0x44u);
			EncodeStr(out, _path);
		} // name index 4

		// :scheme — indexed http(6) / https(7)
		if (_scheme == "http") out.push_back(0x86u);
		else if (_scheme == "https") out.push_back(0x87u);
		else
		{
			out.push_back(0x40u);
			EncodeStr(out, ":scheme");
			EncodeStr(out, _scheme);
		}

		// :authority — name from index 1, literal value
		if (!_authority.empty())
		{
			out.push_back(0x41u);
			EncodeStr(out, _authority);
		}

		// Extra headers — literal without indexing
		for (const auto& [name, value] : _headers)
		{
			out.push_back(0x00u);
			EncodeStr(out, name);
			EncodeStr(out, value);
		}

		return out;
	}

	ne::network::HttpHeaders Decode(const ne::byte_t* _data, std::size_t _len)
	{
		ne::network::HttpHeaders out;
		std::size_t i = 0;

		while (i < _len)
		{
			const uint8_t b = _data[i];

			if (b & 0x80u)
			{
				// Indexed representation
				uint32_t idx;
				if (!ParseInt(_data, _len, i, 7, idx) || idx == 0) break;
				if (idx <= kStaticSize)
				{
					const auto& e = kStatic[idx - 1];
					out.emplace_back(ne::string_t(e.name), ne::string_t(e.value));
				}
				// dynamic table entries: ignored (no dynamic table)
			}
			else if (b & 0x40u)
			{
				// Literal with incremental indexing
				uint32_t nameIdx;
				if (!ParseInt(_data, _len, i, 6, nameIdx)) break;
				ne::string_t name, value;
				if (nameIdx == 0) { if (!ParseStr(_data, _len, i, name)) break; }
				else if (nameIdx <= kStaticSize) name = ne::string_t(kStatic[nameIdx - 1].name);
				if (!ParseStr(_data, _len, i, value)) break;
				out.emplace_back(std::move(name), std::move(value));
			}
			else if (b & 0x20u)
			{
				// Dynamic table size update — skip
				uint32_t sz;
				if (!ParseInt(_data, _len, i, 5, sz)) break;
			}
			else
			{
				// Literal without indexing / never-indexed
				uint32_t nameIdx;
				if (!ParseInt(_data, _len, i, 4, nameIdx)) break;
				ne::string_t name, value;
				if (nameIdx == 0) { if (!ParseStr(_data, _len, i, name)) break; }
				else if (nameIdx <= kStaticSize) name = ne::string_t(kStatic[nameIdx - 1].name);
				if (!ParseStr(_data, _len, i, value)) break;
				out.emplace_back(std::move(name), std::move(value));
			}
		}
		return out;
	}
}

END_NS
