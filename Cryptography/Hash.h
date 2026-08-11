#pragma once
#include "Base/Type.h"

namespace ne::crypto
{
	enum class HashType
	{
		CRC32,
		MD5,
		SHA1,
		SHA2_224,
		SHA2_256,
		SHA2_384,
		SHA2_512,
		SHA3_224,
		SHA3_256,
		SHA3_384,
		SHA3_512
	};

	/** @brief _data 전체를 _type 으로 해시해 소문자 hex 문자열로 반환합니다. */
	[[nodiscard]] string_t Hash(HashType _type, string_view_t _data);

	/** @brief _path 파일 전체를 _type 으로 해시해 소문자 hex 문자열로 반환합니다. 파일을 열 수 없으면 빈 문자열. */
	[[nodiscard]] string_t HashFile(HashType _type, string_view_t _path);
}
