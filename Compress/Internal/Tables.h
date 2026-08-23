//
// Created by hscloud on 26. 8. 23.
//

#pragma once
#include <array>
#include "Base/Type.h"

namespace ne::compress::internal
{
	// RFC 1951 이 정한 상수 표들입니다. 해제기(Inflate)와 압축기(Encode)가 **같은 표를 봐야** 하므로
	// 어느 한쪽의 .cpp 에 두지 않고 여기로 모았습니다 — 복사해 두면 한쪽만 고쳐지는 순간 우리가 만든
	// 스트림을 우리가 못 읽게 되고, 그 증상은 "특정 길이에서만 깨짐" 이라 찾기가 매우 어렵습니다.

	/** @brief §3.2.5 길이 코드 표 — 심볼 257..285 에 대응합니다. */
	inline constexpr std::array<uint16_t, 29> LengthBase = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	inline constexpr std::array<byte_t, 29> LengthExtraBits = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };

	/** @brief §3.2.5 거리 코드 표 — 심볼 0..29 에 대응합니다. */
	inline constexpr std::array<uint16_t, 30> DistanceBase = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
	inline constexpr std::array<byte_t, 30> DistanceExtraBits = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

	/**
	 * @brief §3.2.7: 코드 길이 알파벳(19개)의 길이가 전송되는 순서.
	 *
	 * 자주 쓰이는 값이 앞에 오도록 배열되어 있어, 뒤쪽을 생략(HCLEN)하면 헤더가 짧아집니다.
	 */
	inline constexpr std::array<byte_t, 19> CodeLengthOrder = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

	inline constexpr std::size_t MaxLiteralCodes = 288;
	inline constexpr std::size_t MaxDistanceCodes = 32;

	/** @brief 참조할 수 있는 과거 거리의 상한(32KB 슬라이딩 윈도우). */
	inline constexpr std::size_t WindowSize = 32768;

	/** @brief LZ77 매치의 최소/최대 길이. 3 미만은 리터럴로 내보내는 게 더 짧습니다. */
	inline constexpr std::size_t MinMatchLength = 3;
	inline constexpr std::size_t MaxMatchLength = 258;
}
