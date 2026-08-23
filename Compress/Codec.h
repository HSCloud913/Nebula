//
// Created by hscloud on 26. 8. 12.
//

#pragma once
#include <optional>
#include <span>
#include <vector>
#include "Base/Type.h"
#include "Compress/Deflate.h"
#include "Compress/Diagnostic/Error.h"

namespace ne::compress
{
	/**
	 * @class Encoding
	 * @brief 콘텐츠 인코딩 종류입니다 — HTTP `Content-Encoding`/`Accept-Encoding` 토큰과 1:1 대응합니다.
	 *
	 * @note BROTLI/ZSTD 는 열거값으로만 존재하고 이 빌드에는 구현이 없습니다. 값을 미리 두는 이유는,
	 * 나중에 백엔드를 붙일 때 이 enum 과 그것을 쓰는 코드가 바뀌지 않게 하려는 것입니다 —
	 * IsSupported() 로 분기하면 지원 여부가 늘어나도 호출부는 그대로입니다.
	 */
	enum class Encoding : byte_t
	{
		IDENTITY, // 압축하지 않음
		GZIP,     // RFC 1952 — HTTP 에서 사실상의 표준
		DEFLATE,  // RFC 1950(zlib 컨테이너). 아래 @note 의 함정 참고
		BROTLI,   // RFC 7932 — 미구현(외부 백엔드 예정)
		ZSTD,     // RFC 8878 — 미구현(외부 백엔드 예정)
	};

	/**
	 * @brief 이 빌드가 해당 인코딩을 실제로 처리할 수 있는지.
	 *
	 * @note 지원 목록을 컴파일 타임 상수로 두지 않고 함수로 둔 이유: 외부 백엔드가 CMake 옵션으로
	 * 켜지고 꺼지므로, 호출부가 `#if` 를 흩뿌리지 않고 한 곳에 물어보게 하려는 것입니다.
	 */
	[[nodiscard]] bool_t IsSupported(Encoding _encoding) noexcept;

	/** @brief HTTP 토큰 문자열("gzip", "br" 등)을 Encoding 으로 바꿉니다. 모르는 토큰은 nullopt. */
	[[nodiscard]] std::optional<Encoding> EncodingFromToken(string_view_t _token) noexcept;

	/** @brief Encoding 을 HTTP 토큰 문자열로 바꿉니다(IDENTITY 는 "identity"). */
	[[nodiscard]] string_view_t EncodingToToken(Encoding _encoding) noexcept;

	/**
	 * @brief 이 빌드가 해제할 수 있는 인코딩들을 선호 순서로 나열한 `Accept-Encoding` 헤더 값.
	 *
	 * 클라이언트는 **자기가 해제할 수 있는 것만** 광고해야 합니다 — 광고하지 않은 인코딩은 서버가
	 * 보내지 않으므로, 이 문자열이 곧 "우리가 처리 가능한 범위" 의 선언입니다.
	 */
	[[nodiscard]] string_t AcceptEncodingHeader();

	/**
	 * @brief _encoding 으로 압축된 데이터를 해제합니다.
	 *
	 * @param _maxOutput 결과 크기 상한(압축 폭탄 방어).
	 * @return 해제 결과. IDENTITY 면 입력 복사본. 미지원 인코딩이면 UNSUPPORTED_ENCODING.
	 */
	[[nodiscard]] CompressResult<std::vector<byte_t>> Decode(Encoding _encoding, std::span<const byte_t> _input, std::size_t _maxOutput = DefaultMaxDecompressedSize);

	/**
	 * @brief _input 을 _encoding 으로 압축합니다.
	 *
	 * @return 압축 결과. IDENTITY 면 입력 복사본. 미지원 인코딩이면 UNSUPPORTED_ENCODING.
	 * @note `deflate` 는 규격대로 zlib 컨테이너로 만듭니다 — 해제할 때만 raw 를 함께 받아 줍니다.
	 *       보낼 때는 규격을 따르는 쪽이 옳고, 그렇게 해도 받아들이지 못하는 클라이언트는 없습니다.
	 */
	[[nodiscard]] CompressResult<std::vector<byte_t>> Encode(Encoding _encoding, std::span<const byte_t> _input, int_t _level = DefaultCompressionLevel);

	/**
	 * @brief `Accept-Encoding` 헤더 값에서 우리가 만들 수 있는 것 중 하나를 고릅니다.
	 *
	 * @return 고른 인코딩. 쓸 만한 후보가 없거나 클라이언트가 명시적으로 거부했으면 IDENTITY.
	 * @note 품질값(`q=0`)을 해석합니다 — `gzip;q=0` 은 "gzip 은 보내지 말라" 는 뜻이라 무시하면
	 *       클라이언트가 풀 수 없는 응답을 보내게 됩니다. 그 외의 q 값 순위는 보지 않고 우리 선호를
	 *       씁니다(후보가 gzip/deflate 둘뿐이라 순위를 따져 얻을 것이 없습니다).
	 */
	[[nodiscard]] Encoding SelectEncoding(string_view_t _acceptEncoding) noexcept;
}
