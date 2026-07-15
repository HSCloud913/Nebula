#pragma once
#include "Base/Type.h"

BEGIN_NS(ne)
	/**
	 * @class Base64
	 * @brief 문자열/버퍼에 대한 Base64 인코딩·디코딩을 제공하는 정적 유틸리티 클래스입니다.
	 *
	 * @note EncodeURL/DecodeURL은 URL-safe 알파벳(패딩 없는 `-`/`_`)을 사용하는 변형입니다.
	 */
	class Base64 final
	{
	private:
		explicit Base64() = default;
		~Base64() = default;

	public:
		static void_t Encode(lpcstr_t _string, char_t* _buffer, size_t _bufferSize);
		static void_t Decode(lpcstr_t _string, char_t* _buffer, size_t _bufferSize);

		static string_t Encode(string_t&& _string);
		static string_t Decode(string_t&& _string);

		static string_t EncodeURL(string_t&& _string);
		static string_t DecodeURL(string_t&& _string);
	};

END_NS
