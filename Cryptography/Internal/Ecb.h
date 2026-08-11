//
// Created by hscloud on 26. 8. 6.
//

#pragma once
#include "Base/Type.h"
#include "Cryptography/AES.h"
#include "Cryptography/Diagnostic/Error.h"

namespace ne::crypto::internal
{
	/**
	 * @brief AES-ECB 암·복호 — **NIST 표준 벡터(FIPS 197) 검증 전용 내부 표면입니다.**
	 *
	 * ECB 는 동일 평문 블록이 동일 암호문 블록으로 나타나 데이터 패턴이 그대로 드러나므로 실사용
	 * 금지입니다. 공개 표면에서 제거하고 여기 남긴 이유는 블록 암호 구현 자체를 표준 벡터로
	 * 검증하려면 모드 없는 단일 블록 변환이 필요하기 때문입니다(2026-08-06).
	 */
	[[nodiscard]] CryptoResult<string_t> EncryptEcb(AES::Type _type, string_view_t _key, string_view_t _plaintext);
	[[nodiscard]] CryptoResult<string_t> DecryptEcb(AES::Type _type, string_view_t _key, string_view_t _ciphertext);
}
