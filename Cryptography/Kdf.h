//
// Created by hscloud on 26. 8. 6.
//

#pragma once
#include <cstddef>
#include "Base/Type.h"
#include "Cryptography/Hash.h"
#include "Cryptography/Diagnostic/Error.h"

namespace ne::crypto
{
	/**
	 * @brief HKDF-Extract (RFC 5869 §2.2) — 엔트로피가 고르지 않은 입력 키(IKM)를 고정 길이 PRK 로 응축합니다.
	 *
	 * @param _salt 없어도 되지만(빈 값 허용) 있으면 서로 다른 컨텍스트의 파생 결과를 분리하는 효과가 커집니다.
	 *        비밀이 아니어도 되며, 무작위일수록 좋습니다.
	 * @return 해시 출력 길이만큼의 PRK. _hashType 이 MAC 에 부적합하면(CRC32) INVALID_INPUT.
	 */
	[[nodiscard]] CryptoResult<string_t> HkdfExtract(HashType _hashType, string_view_t _inputKey, string_view_t _salt = {});

	/**
	 * @brief HKDF-Expand (RFC 5869 §2.3) — PRK 를 _info 로 구분된 임의 길이 키 재료로 늘립니다.
	 *
	 * @param _info 용도 라벨. **같은 PRK 에서 서로 다른 용도의 키를 뽑을 때 반드시 다르게** 주세요
	 *        (예: "...-enc" / "...-mac"). 이것이 키 재사용을 막는 유일한 장치입니다.
	 * @param _length 원하는 출력 바이트 수. 해시 출력 길이의 255배를 넘으면 INVALID_INPUT.
	 */
	[[nodiscard]] CryptoResult<string_t> HkdfExpand(HashType _hashType, string_view_t _pseudoRandomKey, string_view_t _info, std::size_t _length);

	/**
	 * @brief HKDF (RFC 5869) 전체 — Extract 후 Expand 를 이어서 수행합니다.
	 *
	 * **이미 균일하게 무작위인 키**(CSPRNG 로 뽑은 32바이트 등)에서 용도별 서브키만 뽑는 경우라면
	 * Extract 를 건너뛰고 HkdfExpand 만 써도 됩니다(RFC 5869 §3.3).
	 */
	[[nodiscard]] CryptoResult<string_t> Hkdf(HashType _hashType, string_view_t _inputKey, string_view_t _salt, string_view_t _info, std::size_t _length);

	/**
	 * @brief PBKDF2 (RFC 2898 §5.2) — **사람이 고른 비밀번호**를 암호 키로 늘립니다.
	 *
	 * 비밀번호는 엔트로피가 낮아 그대로 키로 쓰면 사전 공격에 무너집니다. 이 함수는 HMAC 을
	 * _iterations 회 반복해 시도당 비용을 인위적으로 올립니다.
	 *
	 * @param _salt **사용자마다 다른 무작위 값**을 쓰고 함께 저장하세요(비밀이 아님). 같은 salt 를
	 *        재사용하면 미리 계산된 표(레인보우 테이블) 공격을 막지 못합니다. 16바이트 이상 권장.
	 * @param _iterations 클수록 안전하고 느립니다. 0 이면 INVALID_INPUT.
	 * @param _length 원하는 키 길이(예: AES-256 이면 32).
	 *
	 * @warning 이 구현은 순수 C++ 해시 위에서 도는 참조 구현이라 같은 반복 횟수에서 최적화된
	 *          구현보다 훨씬 느립니다 — 현실적인 반복 횟수를 정할 때 실제 측정치를 기준으로 하세요.
	 *          비밀번호 저장(검증) 용도라면 PBKDF2 보다 메모리 하드 함수(scrypt/Argon2)가 권장되지만
	 *          이 라이브러리에는 아직 없습니다.
	 */
	[[nodiscard]] CryptoResult<string_t> Pbkdf2(HashType _hashType, string_view_t _password, string_view_t _salt, uint_t _iterations, std::size_t _length);
}
