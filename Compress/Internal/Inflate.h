//
// Created by hscloud on 26. 8. 12.
//

#pragma once
#include <span>
#include <vector>
#include "Base/Type.h"
#include "Compress/Diagnostic/Error.h"

namespace ne::compress::internal
{
	/**
	 * @brief raw DEFLATE 스트림(RFC 1951)을 해제합니다 — 컨테이너 헤더/트레일러는 다루지 않습니다.
	 *
	 * @param _input DEFLATE 비트 스트림. 스트림 끝 이후의 바이트(gzip/zlib 트레일러 등)가 뒤에
	 *        붙어 있어도 됩니다 — 마지막 블록을 만나면 멈추고 소비한 길이를 알려줍니다.
	 * @param _maxOutput 해제 결과의 상한(바이트). **압축 폭탄 방어에 필수입니다** — 1KB 입력이
	 *        1GB 로 펼쳐지는 스트림을 만들기는 아주 쉽습니다. 초과 시 OUTPUT_LIMIT_EXCEEDED.
	 * @param _consumed 성공 시 _input 에서 실제로 소비한 바이트 수(트레일러 위치 계산용).
	 * @return 해제된 바이트열, 또는 형식/상한 위반 에러.
	 */
	[[nodiscard]] CompressResult<std::vector<byte_t>> Inflate(std::span<const byte_t> _input, std::size_t _maxOutput, std::size_t& _consumed);
}
