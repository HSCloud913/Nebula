#pragma once
#include "Base/Type.h"
#include "Base/Result.h"

namespace ne::util
{
	/**
	 * @class Base64
	 * @brief Base64(RFC 4648) 인코딩·디코딩 정적 유틸리티입니다.
	 *
	 * 인코딩은 실패하지 않지만(값 반환), 디코딩은 잘못된 문자/길이를 만나면 조용히 오염된 바이트를
	 * 내놓는 대신 ne::Result 로 실패를 명확히 알립니다("예외 없음" 철학). URL-safe 변형은 '-'/'_'
	 * 알파벳을 쓰며 패딩 유무를 인자로 독립 지정할 수 있습니다.
	 */
	class Base64 final
	{
	private:
		explicit Base64() = default;
		~Base64() = default;

	public:
		/** @brief 표준 Base64 인코딩. _isPadded=true 면 '=' 패딩을 채운다. */
		[[nodiscard]] static string_t Encode(string_view_t _data, bool_t _isPadded = true);
		/** @brief URL-safe Base64 인코딩('-'/'_'). 기본은 패딩 없음. */
		[[nodiscard]] static string_t EncodeURL(string_view_t _data, bool_t _isPadded = false);

		/** @brief 표준 Base64 디코딩. 알파벳 밖 문자나 잘린 길이면 Err. '=' 패딩은 있어도/없어도 허용. */
		[[nodiscard]] static ne::Result<string_t> Decode(string_view_t _text);
		/** @brief URL-safe Base64 디코딩. 알파벳 밖 문자나 잘린 길이면 Err. */
		[[nodiscard]] static ne::Result<string_t> DecodeURL(string_view_t _text);
	};
}
