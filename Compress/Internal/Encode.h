//
// Created by hscloud on 26. 8. 23.
//

#pragma once
#include <span>
#include <vector>
#include "Base/Type.h"
#include "Compress/Diagnostic/Error.h"

namespace ne::compress::internal
{
	// 파일 이름이 Deflate.h 가 아닌 이유: 공개 진입점이 이미 Compress/Deflate.h 이고, 한 모듈 안에
	// 같은 이름의 헤더가 둘 있으면 편집기 탭과 include 오타로 계속 헷갈립니다. 함수 이름은 해제기와
	// 대칭이 되도록 Deflate/Inflate 로 두었습니다.

	/**
	 * @brief 바이트열을 raw DEFLATE 스트림(RFC 1951)으로 압축합니다 — 컨테이너 없음.
	 *
	 * @param _level 0..9. 0 은 압축하지 않고 stored 블록으로만 감싸며(그래도 유효한 DEFLATE 스트림),
	 *        1 이 가장 빠르고 9 가 가장 촘촘합니다. 이 값은 매치 탐색을 얼마나 오래 하는지만 바꿉니다.
	 * @return 압축된 바이트열. _level 범위 위반이면 INVALID_ARGUMENT.
	 *
	 * @note 블록마다 stored / 고정 허프만 / 동적 허프만 중 **실제로 가장 짧은 것** 을 고릅니다.
	 * 그래서 압축이 안 되는 입력(이미 압축된 이미지 등)에 대해서도 출력이 입력보다 의미 있게 커지지
	 * 않습니다 — 그 보장이 없으면 서버 응답 압축을 켜는 것 자체가 위험해집니다.
	 */
	[[nodiscard]] CompressResult<std::vector<byte_t>> Deflate(std::span<const byte_t> _input, int_t _level);
}
