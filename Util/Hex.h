//
// Created by hscloud on 26. 8. 6.
//

#pragma once
#include <optional>
#include "Base/Type.h"

namespace ne::util
{
	/**
	 * @class Hex
	 * @brief 바이트열 ↔ 16진 문자열 변환 정적 유틸리티입니다.
	 *
	 * 해시/MAC 출력 표기, 테스트 벡터 입력 등에 씁니다. 인코딩은 항상 소문자를 내보내고,
	 * 디코딩은 홀수 길이나 알파벳 밖 문자를 만나면 조용히 오염된 바이트를 만들지 않고 nullopt 를
	 * 반환합니다(대문자 입력은 허용).
	 */
	class Hex final
	{
	private:
		explicit Hex() = default;
		~Hex() = default;

	public:
		/** @brief 바이트열을 소문자 16진 문자열로 인코딩합니다. */
		[[nodiscard]] static string_t Encode(string_view_t _bytes)
		{
			static constexpr char_t digits[] = "0123456789abcdef";

			string_t result;
			result.reserve(_bytes.size() * 2);
			for (const auto character : _bytes)
			{
				const auto value = static_cast<byte_t>(character);
				result += digits[value >> 4];
				result += digits[value & 0x0F];
			}

			return result;
		}

		/** @brief 16진 문자열을 바이트열로 디코딩합니다. 홀수 길이·비16진 문자면 nullopt. */
		[[nodiscard]] static std::optional<string_t> Decode(const string_view_t _hex)
		{
			if (_hex.size() % 2 != 0) return std::nullopt;

			string_t result;
			result.reserve(_hex.size() / 2);

			for (std::size_t i = 0; i < _hex.size(); i += 2)
			{
				const auto high = DecodeDigit(_hex[i]);
				const auto low = DecodeDigit(_hex[i + 1]);
				if (!high || !low) return std::nullopt;

				result += static_cast<char_t>((*high << 4) | *low);
			}

			return result;
		}

	private:
		[[nodiscard]] static std::optional<byte_t> DecodeDigit(const char_t _character) noexcept
		{
			if (_character >= '0' && _character <= '9') return static_cast<byte_t>(_character - '0');
			if (_character >= 'a' && _character <= 'f') return static_cast<byte_t>(_character - 'a' + 10);
			if (_character >= 'A' && _character <= 'F') return static_cast<byte_t>(_character - 'A' + 10);

			return std::nullopt;
		}
	};
}
