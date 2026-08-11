//
// Created by hscloud on 26. 8. 6.
//

#pragma once
#include <cstddef>
#include "Base/Type.h"

namespace ne::util
{
	/**
	 * @brief 버퍼를 0 으로 덮어씁니다 — 키/평문 등 비밀 값이 해제된 메모리에 남지 않게 하려는 용도입니다.
	 *
	 * volatile 로 써서 "쓴 뒤 바로 버리는 코드"를 컴파일러가 죽은 저장(dead store)으로 제거하는 것을
	 * 어렵게 만듭니다.
	 *
	 * @warning **best-effort 입니다.** 컴파일러/최적화 구현에 따라 제거될 여지가 완전히 없지는 않고,
	 *          이미 만들어진 복사본(재할당으로 이동된 옛 버퍼, 스왑 파일, 코어덤프, 레지스터 잔여)까지
	 *          지우지는 못합니다. 비밀 값의 수명을 짧게 유지하는 설계가 우선이고 이건 마지막 방어선입니다.
	 */
	inline void_t SecureWipe(void_t* _buffer, const std::size_t _length) noexcept
	{
		if (_buffer == nullptr || _length == 0) return;

		volatile auto* bytes = static_cast<volatile byte_t*>(_buffer);
		for (std::size_t i = 0; i < _length; ++i) bytes[i] = 0;
	}

	/** @brief 문자열이 담고 있는 바이트를 0 으로 덮어씁니다(길이는 그대로 — 내용만 소거). */
	inline void_t SecureWipe(string_t& _text) noexcept { SecureWipe(_text.data(), _text.size()); }
}
