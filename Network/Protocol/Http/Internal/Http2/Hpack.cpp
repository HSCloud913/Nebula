//
// Created by hscloud on 26. 7. 28.
//

#include "Network/Protocol/Http/Internal/Http2/Hpack.h"

#include <array>
#include <map>
#include <utility>

namespace ne::network::http_2::internal
{
	namespace
	{
		// ── HPACK 정적 테이블 (RFC 7541 Appendix A). 인덱스 1..61. ──
		struct StaticEntry { string_view_t name; string_view_t value; };
		constexpr std::array<StaticEntry, 61> StaticTable = { {
			{ ":authority", "" },
			{ ":method", "GET" },
			{ ":method", "POST" },
			{ ":path", "/" },
			{ ":path", "/index.html" },
			{ ":scheme", "http" },
			{ ":scheme", "https" },
			{ ":status", "200" },
			{ ":status", "204" },
			{ ":status", "206" },
			{ ":status", "304" },
			{ ":status", "400" },
			{ ":status", "404" },
			{ ":status", "500" },
			{ "accept-charset", "" },
			{ "accept-encoding", "gzip, deflate" },
			{ "accept-language", "" },
			{ "accept-ranges", "" },
			{ "accept", "" },
			{ "access-control-allow-origin", "" },
			{ "age", "" },
			{ "allow", "" },
			{ "authorization", "" },
			{ "cache-control", "" },
			{ "content-disposition", "" },
			{ "content-encoding", "" },
			{ "content-language", "" },
			{ "content-length", "" },
			{ "content-location", "" },
			{ "content-range", "" },
			{ "content-type", "" },
			{ "cookie", "" },
			{ "date", "" },
			{ "etag", "" },
			{ "expect", "" },
			{ "expires", "" },
			{ "from", "" },
			{ "host", "" },
			{ "if-match", "" },
			{ "if-modified-since", "" },
			{ "if-none-match", "" },
			{ "if-range", "" },
			{ "if-unmodified-since", "" },
			{ "last-modified", "" },
			{ "link", "" },
			{ "location", "" },
			{ "max-forwards", "" },
			{ "proxy-authenticate", "" },
			{ "proxy-authorization", "" },
			{ "range", "" },
			{ "referer", "" },
			{ "refresh", "" },
			{ "retry-after", "" },
			{ "server", "" },
			{ "set-cookie", "" },
			{ "strict-transport-security", "" },
			{ "transfer-encoding", "" },
			{ "user-agent", "" },
			{ "vary", "" },
			{ "via", "" },
			{ "www-authenticate", "" },
		} };

		// ── HPACK Huffman 코드 테이블 (RFC 7541 Appendix B), 심볼 0..127. [symbol] = {code, bits}. ──
		// @note 128..255(비-ASCII)는 실 HTTP 헤더에 사실상 나타나지 않아 의도적으로 제외한다. 해당 코드가
		//       등장하면 HuffmanDecode 가 매칭 실패로 nullopt(=COMPRESSION_ERROR)를 반환한다. 우리 인코더는
		//       비-Huffman 리터럴만 내보내므로 루프백 경로에서 이 테이블은 애초에 쓰이지 않는다.
		struct HuffCode { std::uint32_t code; byte_t bits; };
		constexpr std::array<HuffCode, 128> HuffmanTable = { {
			{ 0x1ff8, 13 }, { 0x7fffd8, 23 }, { 0xfffffe2, 28 }, { 0xfffffe3, 28 },
			{ 0xfffffe4, 28 }, { 0xfffffe5, 28 }, { 0xfffffe6, 28 }, { 0xfffffe7, 28 },
			{ 0xfffffe8, 28 }, { 0xffffea, 24 }, { 0x3ffffffc, 30 }, { 0xfffffe9, 28 },
			{ 0xfffffea, 28 }, { 0x3ffffffd, 30 }, { 0xfffffeb, 28 }, { 0xfffffec, 28 },
			{ 0xfffffed, 28 }, { 0xfffffee, 28 }, { 0xfffffef, 28 }, { 0xffffff0, 28 },
			{ 0xffffff1, 28 }, { 0xffffff2, 28 }, { 0x3ffffffe, 30 }, { 0xffffff3, 28 },
			{ 0xffffff4, 28 }, { 0xffffff5, 28 }, { 0xffffff6, 28 }, { 0xffffff7, 28 },
			{ 0xffffff8, 28 }, { 0xffffff9, 28 }, { 0xffffffa, 28 }, { 0xffffffb, 28 },
			{ 0x14, 6 }, { 0x3f8, 10 }, { 0x3f9, 10 }, { 0xffa, 12 },
			{ 0x1ffb, 13 }, { 0x15, 6 }, { 0xf8, 8 }, { 0xffb, 12 },
			{ 0x3fa, 10 }, { 0x3fb, 10 }, { 0xf9, 8 }, { 0xffc, 12 },
			{ 0xfa, 8 }, { 0x16, 6 }, { 0x17, 6 }, { 0x18, 6 },
			{ 0x0, 5 }, { 0x1, 5 }, { 0x2, 5 }, { 0x19, 6 },
			{ 0x1a, 6 }, { 0x1b, 6 }, { 0x1c, 6 }, { 0x1d, 6 },
			{ 0x1e, 6 }, { 0x1f, 6 }, { 0x5c, 7 }, { 0xfb, 8 },
			{ 0x7ffc, 15 }, { 0x20, 6 }, { 0xffd, 12 }, { 0x3fc, 10 },
			{ 0x1ffc, 13 }, { 0x21, 6 }, { 0x5d, 7 }, { 0x5e, 7 },
			{ 0x5f, 7 }, { 0x60, 7 }, { 0x61, 7 }, { 0x62, 7 },
			{ 0x63, 7 }, { 0x64, 7 }, { 0x65, 7 }, { 0x66, 7 },
			{ 0x67, 7 }, { 0x68, 7 }, { 0x69, 7 }, { 0x6a, 7 },
			{ 0x6b, 7 }, { 0x6c, 7 }, { 0x6d, 7 }, { 0x6e, 7 },
			{ 0x6f, 7 }, { 0x70, 7 }, { 0x71, 7 }, { 0x72, 7 },
			{ 0xfc, 8 }, { 0x73, 7 }, { 0xfd, 8 }, { 0x1ffd, 13 },
			{ 0x7fff0, 19 }, { 0x1ffe, 13 }, { 0x3ffc, 14 }, { 0x22, 6 },
			{ 0x7ffd, 15 }, { 0x3, 5 }, { 0x23, 6 }, { 0x4, 5 },
			{ 0x24, 6 }, { 0x5, 5 }, { 0x25, 6 }, { 0x26, 6 },
			{ 0x27, 6 }, { 0x6, 5 }, { 0x74, 7 }, { 0x75, 7 },
			{ 0x28, 6 }, { 0x29, 6 }, { 0x2a, 6 }, { 0x7, 5 },
			{ 0x2b, 6 }, { 0x76, 7 }, { 0x2c, 6 }, { 0x8, 5 },
			{ 0x9, 5 }, { 0x2d, 6 }, { 0x77, 7 }, { 0x78, 7 },
			{ 0x79, 7 }, { 0x7a, 7 }, { 0x7b, 7 }, { 0x7ffe, 15 },
			{ 0x7fc, 11 }, { 0x3ffd, 14 }, { 0x1fff, 13 }, { 0xffffffc, 28 },
		} };

		// (bits,code) → symbol 역인덱스. 프로그램 수명 동안 1회 구성.
		const std::map<std::pair<byte_t, std::uint32_t>, int_t>& HuffmanReverse()
		{
			static const std::map<std::pair<byte_t, std::uint32_t>, int_t> table = []
			{
				std::map<std::pair<byte_t, std::uint32_t>, int_t> map;
				for (int_t symbol = 0; symbol < static_cast<int_t>(HuffmanTable.size()); ++symbol) map.emplace(std::pair{ HuffmanTable[symbol].bits, HuffmanTable[symbol].code }, symbol);
				return map;
			}();
			return table;
		}

		// prefixBits 접두 정수 디코딩(RFC 7541 §5.1). 성공 시 _pos 를 소비 위치로 전진.
		bool_t DecodeInteger(const std::span<const byte_t> _data, std::size_t& _pos, const int_t _prefixBits, std::uint64_t& _out) noexcept
		{
			if (_pos >= _data.size()) return false;

			const std::uint32_t mask = (1u << _prefixBits) - 1u;
			std::uint64_t value = _data[_pos++] & mask;
			if (value < mask)
			{
				_out = value;
				return true;
			}

			std::uint64_t shift = 0;
			byte_t byte;
			do
			{
				if (_pos >= _data.size() || shift > 63) return false;
				byte = _data[_pos++];
				value += static_cast<std::uint64_t>(byte & 0x7F) << shift;
				shift += 7;
			}
			while (byte & 0x80);

			_out = value;
			return true;
		}

		// 문자열 리터럴 디코딩(H 비트 + 길이 + 바이트열, RFC 7541 §5.2). 성공 시 _pos 전진.
		bool_t DecodeStringLiteral(const std::span<const byte_t> _data, std::size_t& _pos, string_t& _out)
		{
			if (_pos >= _data.size()) return false;

			const bool_t huffman = (_data[_pos] & 0x80) != 0;
			std::uint64_t length = 0;
			if (!DecodeInteger(_data, _pos, 7, length)) return false;
			if (_pos + length > _data.size()) return false;

			const auto bytes = _data.subspan(_pos, static_cast<std::size_t>(length));
			_pos += static_cast<std::size_t>(length);

			if (!huffman)
			{
				_out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
				return true;
			}

			const auto decoded = HuffmanDecode(bytes);
			if (!decoded) return false;

			_out.assign(reinterpret_cast<const char*>(decoded->data()), decoded->size());
			return true;
		}

		// prefixBits 접두 정수 인코딩(RFC 7541 §5.1). _first 는 상위 비트가 이미 채워진 첫 바이트.
		void_t EncodeInteger(std::vector<byte_t>& _out, std::uint64_t _value, const int_t _prefixBits, const byte_t _first)
		{
			const std::uint32_t mask = (1u << _prefixBits) - 1u;
			if (_value < mask)
			{
				_out.push_back(static_cast<byte_t>(_first | _value));
				return;
			}

			_out.push_back(static_cast<byte_t>(_first | mask));
			_value -= mask;
			while (_value >= 128)
			{
				_out.push_back(static_cast<byte_t>((_value & 0x7F) | 0x80));
				_value >>= 7;
			}
			_out.push_back(static_cast<byte_t>(_value));
		}

		// 문자열 리터럴 인코딩(비-Huffman, H=0).
		void_t EncodeStringLiteral(std::vector<byte_t>& _out, const string_view_t _value)
		{
			EncodeInteger(_out, _value.size(), 7, 0x00);
			_out.insert(_out.end(), _value.begin(), _value.end());
		}
	}



	std::optional<std::vector<byte_t>> HuffmanDecode(const std::span<const byte_t> _input)
	{
		const auto& reverse = HuffmanReverse();

		std::vector<byte_t> out;
		std::uint32_t current = 0;
		byte_t bits = 0;

		for (const byte_t byte : _input)
		{
			for (int_t i = 7; i >= 0; --i)
			{
				current = (current << 1) | ((byte >> i) & 0x1);
				++bits;
				if (bits > 30) return std::nullopt; // 어떤 코드도 30비트를 넘지 않음 → 오정렬

				if (const auto iter = reverse.find({ bits, current }); iter != reverse.end())
				{
					if (iter->second == 256) return std::nullopt; // EOS 는 디코딩되면 안 됨
					out.push_back(static_cast<byte_t>(iter->second));
					current = 0;
					bits = 0;
				}
			}
		}

		// 남은 비트는 7비트 이하이며 전부 1(EOS 접두 패딩)이어야 유효.
		if (bits > 7) return std::nullopt;
		if (bits > 0)
		{
			const std::uint32_t pad = (1u << bits) - 1u;
			if ((current & pad) != pad) return std::nullopt;
		}

		return out;
	}



	// ───────────────────────── HpackDecoder ─────────────────────────

	bool_t HpackDecoder::Lookup(const std::size_t _index, string_t& _name, string_t& _value) const
	{
		if (_index == 0) return false;

		if (_index <= StaticTable.size())
		{
			const auto& entry = StaticTable[_index - 1];
			_name.assign(entry.name);
			_value.assign(entry.value);
			return true;
		}

		const std::size_t dynIndex = _index - StaticTable.size() - 1; // 0 = 가장 최근
		if (dynIndex >= dynamicTable.size()) return false;

		_name = dynamicTable[dynIndex].name;
		_value = dynamicTable[dynIndex].value;
		return true;
	}

	void_t HpackDecoder::AddToDynamicTable(string_t _name, string_t _value)
	{
		Entry entry{ std::move(_name), std::move(_value) };
		const std::size_t entrySize = entry.Size();

		dynamicTable.push_front(std::move(entry));
		dynamicSize += entrySize;

		while (dynamicSize > maxDynamicSize && !dynamicTable.empty())
		{
			dynamicSize -= dynamicTable.back().Size();
			dynamicTable.pop_back();
		}
	}

	void_t HpackDecoder::SetMaxDynamicSize(const std::size_t _size)
	{
		maxDynamicSize = _size;
		while (dynamicSize > maxDynamicSize && !dynamicTable.empty())
		{
			dynamicSize -= dynamicTable.back().Size();
			dynamicTable.pop_back();
		}
	}

	std::optional<HeaderList> HpackDecoder::Decode(const std::span<const byte_t> _block)
	{
		HeaderList result;
		std::size_t pos = 0;

		while (pos < _block.size())
		{
			const byte_t first = _block[pos];

			if (first & 0x80) // 1xxxxxxx : Indexed Header Field
			{
				std::uint64_t index = 0;
				if (!DecodeInteger(_block, pos, 7, index)) return std::nullopt;

				string_t name, value;
				if (!Lookup(static_cast<std::size_t>(index), name, value)) return std::nullopt;
				result.push_back(HpackHeader{ std::move(name), std::move(value) });
			}
			else if (first & 0x40) // 01xxxxxx : Literal with Incremental Indexing
			{
				std::uint64_t index = 0;
				if (!DecodeInteger(_block, pos, 6, index)) return std::nullopt;

				string_t name, value;
				if (index != 0)
				{
					string_t ignoredValue;
					if (!Lookup(static_cast<std::size_t>(index), name, ignoredValue)) return std::nullopt;
				}
				else if (!DecodeStringLiteral(_block, pos, name)) return std::nullopt;

				if (!DecodeStringLiteral(_block, pos, value)) return std::nullopt;

				AddToDynamicTable(name, value);
				result.push_back(HpackHeader{ std::move(name), std::move(value) });
			}
			else if (first & 0x20) // 001xxxxx : Dynamic Table Size Update
			{
				std::uint64_t size = 0;
				if (!DecodeInteger(_block, pos, 5, size)) return std::nullopt;
				SetMaxDynamicSize(static_cast<std::size_t>(size));
			}
			else // 0000xxxx / 0001xxxx : Literal without Indexing / Never Indexed
			{
				std::uint64_t index = 0;
				if (!DecodeInteger(_block, pos, 4, index)) return std::nullopt;

				string_t name, value;
				if (index != 0)
				{
					string_t ignoredValue;
					if (!Lookup(static_cast<std::size_t>(index), name, ignoredValue)) return std::nullopt;
				}
				else if (!DecodeStringLiteral(_block, pos, name)) return std::nullopt;

				if (!DecodeStringLiteral(_block, pos, value)) return std::nullopt;
				result.push_back(HpackHeader{ std::move(name), std::move(value) });
			}
		}

		return result;
	}



	// ───────────────────────── HpackEncoder ─────────────────────────

	void_t HpackEncoder::Encode(const HeaderList& _headers, std::vector<byte_t>& _out) const
	{
		for (const auto& header : _headers)
		{
			_out.push_back(0x00); // Literal without Indexing, 새 이름(인덱스 0)
			EncodeStringLiteral(_out, header.name);
			EncodeStringLiteral(_out, header.value);
		}
	}
}
