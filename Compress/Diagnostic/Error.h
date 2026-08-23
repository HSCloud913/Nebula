//
// Created by hscloud on 26. 8. 12.
//

#pragma once
#include "Base/Type.h"
#include "Base/Error.h"
#include "Base/Result.h"

namespace ne::compress
{
	/**
	 * @class CompressErrorKind
	 * @brief 압축/해제 계층 에러 분류 열거형.
	 */
	enum class CompressErrorKind : byte_t
	{
		MALFORMED_STREAM,   // 비트 스트림이 형식을 위반함(잘못된 블록 타입, 깨진 허프만 표 등)
		TRUNCATED_STREAM,   // 스트림이 끝나기 전에 입력이 고갈됨
		CHECKSUM_MISMATCH,  // CRC32/Adler-32 불일치 — 전송 중 손상되었거나 다른 스트림
		OUTPUT_LIMIT_EXCEEDED, // 해제 결과가 허용 상한을 넘음(압축 폭탄 방어)
		UNSUPPORTED_ENCODING,  // 이 빌드가 그 인코딩(예: br)을 지원하지 않음
		INVALID_ARGUMENT,      // 압축 레벨 범위 위반 등 호출자 오류
	};

	/**
	 * @class CompressError
	 * @brief ne::Error 를 상속한 압축 계층 전용 에러 타입입니다(CryptoError/IoError 와 동일한 패턴).
	 *
	 * @note MALFORMED_STREAM 과 TRUNCATED_STREAM 을 구분하는 이유는 스트리밍 해제에서 후자가
	 * "아직 더 오면 된다" 는 뜻일 수 있기 때문입니다 — 전자는 재시도해도 의미가 없습니다.
	 */
	class CompressError final :public ne::Error
	{
	public:
		explicit CompressError(const CompressErrorKind _kind, const string_view_t _message = {})
			: Error(_message.empty() ? DefaultMessage(_kind) : string_t{ _message })
			, kind(_kind) {}

	private:
		CompressErrorKind kind;

	public:
		CompressError& Context(const string_view_t _context)
		{
			Error::Context(_context);
			return *this;
		}

		[[nodiscard]] CompressErrorKind Kind() const noexcept { return kind; }
		[[nodiscard]] bool_t IsUnsupported() const noexcept { return kind == CompressErrorKind::UNSUPPORTED_ENCODING; }

	private:
		[[nodiscard]] static string_t DefaultMessage(const CompressErrorKind _kind)
		{
			switch (_kind)
			{
				case CompressErrorKind::MALFORMED_STREAM:
					return "malformed compressed stream";
				case CompressErrorKind::TRUNCATED_STREAM:
					return "compressed stream ended unexpectedly";
				case CompressErrorKind::CHECKSUM_MISMATCH:
					return "checksum mismatch";
				case CompressErrorKind::OUTPUT_LIMIT_EXCEEDED:
					return "decompressed size exceeds limit";
				case CompressErrorKind::UNSUPPORTED_ENCODING:
					return "encoding not supported by this build";
				case CompressErrorKind::INVALID_ARGUMENT:
					return "invalid argument";
			}

			return "unknown compress error";
		}
	};

	template <typename T>
	using CompressResult = ne::Result<T, CompressError>;
}
