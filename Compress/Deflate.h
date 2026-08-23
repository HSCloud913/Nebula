//
// Created by hscloud on 26. 8. 12.
//

#pragma once
#include <span>
#include <vector>
#include "Base/Type.h"
#include "Compress/Diagnostic/Error.h"

namespace ne::compress
{
	/**
	 * @brief 해제 결과 크기의 기본 상한(64MB).
	 *
	 * @note 상한을 두는 이유는 **압축 폭탄**입니다. DEFLATE 는 최대 1032:1 로 펼쳐질 수 있어, 1MB
	 * 입력이 1GB 를 요구할 수 있습니다. 신뢰할 수 없는 입력(HTTP 응답 등)을 해제할 때 상한이 없으면
	 * 그것만으로 메모리를 고갈시킬 수 있습니다. 기본값은 Http::Limits 의 본문 상한과 같게 맞췄습니다.
	 */
	inline constexpr std::size_t DefaultMaxDecompressedSize = 64 * 1024 * 1024;

	/**
	 * @brief raw DEFLATE 스트림(RFC 1951)을 해제합니다 — 컨테이너 헤더/트레일러 없음.
	 *
	 * @note HTTP 의 `Content-Encoding: deflate` 는 규격상 zlib 컨테이너(RFC 1950)를 뜻하므로
	 * ZlibDecompress() 를 쓰세요. 다만 과거 IIS 가 컨테이너 없는 raw DEFLATE 를 보낸 탓에 양쪽을
	 * 모두 받아들이는 클라이언트가 많습니다 — 그 호환 경로에 이 함수가 필요합니다.
	 */
	[[nodiscard]] CompressResult<std::vector<byte_t>> RawInflate(std::span<const byte_t> _input, std::size_t _maxOutput = DefaultMaxDecompressedSize);

	/** @brief zlib 컨테이너(RFC 1950: 2바이트 헤더 + DEFLATE + Adler-32)를 해제합니다. */
	[[nodiscard]] CompressResult<std::vector<byte_t>> ZlibDecompress(std::span<const byte_t> _input, std::size_t _maxOutput = DefaultMaxDecompressedSize);



	/**
	 * @brief 압축 레벨의 기본값.
	 *
	 * @note 9(최대)를 기본으로 두지 않는 이유는, 6→9 가 크기를 몇 % 줄이는 대가로 매치 탐색을 수십 배
	 * 더 하기 때문입니다. 응답을 요청마다 압축하는 서버에서는 그 교환이 거의 항상 손해입니다.
	 */
	inline constexpr int_t DefaultCompressionLevel = 6;

	/**
	 * @brief raw DEFLATE 스트림(RFC 1951)으로 압축합니다 — 컨테이너 없음.
	 *
	 * @param _level 0(비압축 stored) ~ 9(최대). 범위를 벗어나면 INVALID_ARGUMENT.
	 * @note 블록마다 stored/고정/동적 허프만 중 가장 짧은 것을 고르므로, 이미 압축된 입력이 들어와도
	 * 출력이 입력보다 의미 있게 커지지 않습니다.
	 */
	[[nodiscard]] CompressResult<std::vector<byte_t>> RawDeflate(std::span<const byte_t> _input, int_t _level = DefaultCompressionLevel);

	/** @brief zlib 컨테이너로 압축합니다(HTTP `Content-Encoding: deflate` 가 규격상 뜻하는 형식). */
	[[nodiscard]] CompressResult<std::vector<byte_t>> ZlibCompress(std::span<const byte_t> _input, int_t _level = DefaultCompressionLevel);
}
