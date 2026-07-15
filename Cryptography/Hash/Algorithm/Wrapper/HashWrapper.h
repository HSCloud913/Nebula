#pragma once
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
	/**
	 * @class HashWrapper
	 * @brief 문자열/파일 해시 계산을 위한 공통 인터페이스입니다.
	 *
	 * 각 알고리즘(CRC32/MD5/SHA1/SHA2/SHA3)별 Wrapper 파생 클래스가 OnGetHashFromString/
	 * OnGetHashFromFile을 구현하며, 공개 오버로드는 이를 감싸 문자열 반환/버퍼 기입 두 방식을 제공합니다.
	 */
	class HashWrapper
	{
	public:
		explicit HashWrapper() = default;
		virtual ~HashWrapper() = default;

	public:
		[[nodiscard]] string_t GetHashFromString(string_t&& _text);
		void_t GetHashFromString(char_t* _buffer, size_t _bufferSize, lpcstr_t _text);

		[[nodiscard]] string_t GetHashFromFile(string_t&& _filePath);
		void_t GetHashFromFile(char_t* _buffer, size_t _bufferSize, lpcstr_t _filePath);

	private:
		[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) = 0;
		[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) = 0;
	};

END_NS
