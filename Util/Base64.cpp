#include "Util/Base64.h"

#include <array>
#include <cstdint>
#include <utility>

namespace ne::util
{
	namespace
	{
		constexpr string_view_t StandardAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		constexpr string_view_t UrlAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

		string_t EncodeWith(const string_view_t _data, const string_view_t _alphabet, const bool_t _padding)
		{
			string_t out;
			out.reserve((_data.size() + 2) / 3 * 4);

			const auto* data = reinterpret_cast<const byte_t*>(_data.data());
			const std::size_t size = _data.size();

			std::size_t i = 0;
			for (; i + 3 <= size; i += 3)
			{
				const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16) | (static_cast<std::uint32_t>(data[i + 1]) << 8) | data[i + 2];
				out += _alphabet[(triple >> 18) & 0x3F];
				out += _alphabet[(triple >> 12) & 0x3F];
				out += _alphabet[(triple >> 6) & 0x3F];
				out += _alphabet[triple & 0x3F];
			}

			if (const std::size_t remainder = size - i; remainder == 1)
			{
				const std::uint32_t triple = static_cast<std::uint32_t>(data[i]) << 16;
				out += _alphabet[(triple >> 18) & 0x3F];
				out += _alphabet[(triple >> 12) & 0x3F];
				if (_padding) out += "==";
			}
			else if (remainder == 2)
			{
				const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16) | (static_cast<std::uint32_t>(data[i + 1]) << 8);
				out += _alphabet[(triple >> 18) & 0x3F];
				out += _alphabet[(triple >> 12) & 0x3F];
				out += _alphabet[(triple >> 6) & 0x3F];
				if (_padding) out += "=";
			}

			return out;
		}

		ne::Result<string_t> DecodeWith(const string_view_t _text, const string_view_t _alphabet)
		{
			using R = ne::Result<string_t>;

			std::array<int_t, 256> reverse{};
			reverse.fill(-1);
			for (std::size_t i = 0; i < _alphabet.size(); ++i) reverse[static_cast<byte_t>(_alphabet[i])] = static_cast<int_t>(i);

			// 후행 '=' 패딩만 허용(0~2개). 중간에 나오는 '='는 아래 루프에서 알파벳 밖 문자로 잡힌다.
			std::size_t end = _text.size();
			std::size_t padding = 0;
			while (end > 0 && _text[end - 1] == '=' && padding < 3) { --end; ++padding; }
			if (padding > 2) return R::Error(ne::Error{ "base64: invalid padding" });

			string_t out;
			out.reserve(end / 4 * 3 + 3);

			std::uint32_t buffer = 0;
			int_t bits = 0;
			for (std::size_t i = 0; i < end; ++i)
			{
				const int_t value = reverse[static_cast<byte_t>(_text[i])];
				if (value < 0) return R::Error(ne::Error{ "base64: invalid character" });

				buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					out += static_cast<char_t>((buffer >> bits) & 0xFF);
				}
			}

			// 남은 6비트 그룹 하나(길이 %4 == 1)는 온전한 바이트를 만들 수 없는 잘린 입력이다.
			if (bits == 6) return R::Error(ne::Error{ "base64: truncated input" });

			return R::Ok(std::move(out));
		}
	}



	string_t Base64::Encode(const string_view_t _data, const bool_t _padding) { return EncodeWith(_data, StandardAlphabet, _padding); }
	string_t Base64::EncodeURL(const string_view_t _data, const bool_t _padding) { return EncodeWith(_data, UrlAlphabet, _padding); }

	ne::Result<string_t> Base64::Decode(const string_view_t _text) { return DecodeWith(_text, StandardAlphabet); }
	ne::Result<string_t> Base64::DecodeURL(const string_view_t _text) { return DecodeWith(_text, UrlAlphabet); }
}
