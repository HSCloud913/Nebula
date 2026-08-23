//
// Created by hscloud on 26. 8. 12.
//

#pragma once
#include <span>
#include <vector>
#include "Base/Type.h"
#include "Compress/Deflate.h"
#include "Compress/Diagnostic/Error.h"

namespace ne::compress
{
	/**
	 * @brief gzip 스트림(RFC 1952: 헤더 + DEFLATE + CRC32 + ISIZE)을 해제합니다.
	 *
	 * gzip 은 HTTP 에서 사실상의 표준 압축입니다 — 모든 클라이언트가 받아들이므로, 서버가 gzip 만
	 * 제공해도 호환성 문제가 없습니다.
	 *
	 * @param _maxOutput 해제 결과 상한(압축 폭탄 방어 — DefaultMaxDecompressedSize 주석 참고).
	 * @note 여러 gzip 멤버가 이어 붙은 스트림(concatenated members)은 첫 멤버만 해제합니다. 그 형식은
	 * 파일 도구(`cat a.gz b.gz`)에서 나오며 HTTP 응답에서는 나타나지 않습니다.
	 */
	[[nodiscard]] CompressResult<std::vector<byte_t>> GzipDecompress(std::span<const byte_t> _input, std::size_t _maxOutput = DefaultMaxDecompressedSize);

	/**
	 * @brief gzip 스트림으로 압축합니다.
	 *
	 * @param _level 0(비압축) ~ 9(최대). 범위를 벗어나면 INVALID_ARGUMENT.
	 * @note MTIME 은 0(시각 정보 없음)으로 둡니다 — 같은 입력이 항상 같은 바이트로 나와야 캐시 검증
	 * (ETag)과 재현 가능한 빌드가 성립합니다. 시각을 넣으면 내용이 같은데도 매번 다른 응답이 됩니다.
	 */
	[[nodiscard]] CompressResult<std::vector<byte_t>> GzipCompress(std::span<const byte_t> _input, int_t _level = DefaultCompressionLevel);
}
