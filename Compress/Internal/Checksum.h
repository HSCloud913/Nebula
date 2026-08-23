//
// Created by hscloud on 26. 8. 12.
//

#pragma once
#include <span>
#include "Base/Type.h"

namespace ne::compress::internal
{
	/**
	 * @brief CRC-32(IEEE 802.3, 반사 다항식 0xEDB88320) — gzip 트레일러가 요구하는 체크섬입니다.
	 *
	 * @note Cryptography 의 CRC32 를 재사용하지 않는 이유가 둘 있습니다. 그쪽은 결과를 16진 문자열로
	 * 돌려주는데 여기는 원시 32비트 값이 필요하고, 무엇보다 압축이 암호 모듈에 의존하는 것은 방향이
	 * 어색합니다(체크섬은 무결성 확인용이지 암호가 아닙니다). 표 256칸 + 루프 한 개의 중복을 감수합니다.
	 * @param _seed 이어서 계산할 때 이전 반환값을 넘깁니다(처음에는 0).
	 */
	[[nodiscard]] uint_t Crc32(std::span<const byte_t> _data, uint_t _seed = 0) noexcept;

	/**
	 * @brief Adler-32(RFC 1950 §9) — zlib 트레일러가 요구하는 체크섬입니다.
	 *
	 * CRC32 보다 빠르지만 짧은 입력에서 충돌 내성이 약합니다. zlib 형식이 지정한 것이므로 선택의
	 * 여지는 없습니다.
	 * @param _seed 이어서 계산할 때 이전 반환값을 넘깁니다(처음에는 1 — 규격이 정한 초기값).
	 */
	[[nodiscard]] uint_t Adler32(std::span<const byte_t> _data, uint_t _seed = 1) noexcept;
}
