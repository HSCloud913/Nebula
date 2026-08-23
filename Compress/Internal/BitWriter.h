//
// Created by hscloud on 26. 8. 23.
//

#pragma once
#include <vector>
#include "Base/Type.h"

namespace ne::compress::internal
{
	/**
	 * @class BitWriter
	 * @brief DEFLATE 비트 스트림을 만드는 출력기입니다 — BitReader 의 대칭입니다.
	 *
	 * BitReader 와 같은 비대칭 규칙을 지킵니다. **값(길이/거리의 여분 비트, 블록 헤더)은 LSB 부터**
	 * 채우고, **허프만 코드는 MSB 부터** 채웁니다. 그래서 코드 출력용 Write() 와 값 출력용
	 * WriteBits() 를 따로 둡니다 — 하나로 합치면 호출부마다 비트를 뒤집는 코드가 흩어집니다.
	 */
	class BitWriter
	{
	public:
		BitWriter() = default;

	private:
		std::vector<byte_t> bytes;
		uint_t accumulator{ 0 }; // 아직 바이트로 못 내보낸 비트들(LSB 쪽이 먼저 나갈 비트)
		int_t bitCount{ 0 };     // accumulator 에 든 비트 수

	public:
		/** @brief _value 의 하위 _count 비트를 LSB 부터 씁니다(여분 비트·블록 헤더용). */
		void_t WriteBits(const uint_t _value, const int_t _count)
		{
			accumulator |= (_value & ((1u << _count) - 1u)) << bitCount;
			bitCount += _count;

			Flush();
		}

		/**
		 * @brief 허프만 코드 _code(길이 _length)를 씁니다 — MSB 부터 나가도록 비트를 뒤집어 넣습니다.
		 *
		 * @note 규격이 이렇게 정한 이유는 복호기가 비트를 하나씩 읽으며 코드를 왼쪽부터 조립할 수
		 * 있게 하려는 것입니다. 여기서 뒤집어 두지 않으면 우리 스트림은 어떤 표준 해제기도 읽지 못합니다.
		 */
		void_t WriteCode(const uint_t _code, const int_t _length)
		{
			for (int_t bit = _length - 1; bit >= 0; --bit)
			{
				accumulator |= ((_code >> bit) & 1u) << bitCount;
				++bitCount;

				Flush();
			}
		}

		/** @brief 현재 바이트 경계까지 0 으로 채웁니다(stored 블록은 바이트 정렬을 요구합니다). */
		void_t AlignToByte()
		{
			if (bitCount % 8 != 0) WriteBits(0, 8 - (bitCount % 8));
		}

		/** @brief 바이트 정렬된 상태에서 원시 바이트를 그대로 덧붙입니다(stored 블록 본문). */
		void_t WriteBytes(const std::vector<byte_t>& _raw) { bytes.insert(bytes.end(), _raw.begin(), _raw.end()); }
		void_t WriteByte(const byte_t _value) { bytes.push_back(_value); }

		/** @brief 남은 비트를 0 으로 메워 바이트로 내보내고 결과를 돌려줍니다(이후 이 객체는 쓰지 마세요). */
		[[nodiscard]] std::vector<byte_t> Finish()
		{
			AlignToByte();

			return std::move(bytes);
		}

		[[nodiscard]] std::size_t ByteSize() const noexcept { return bytes.size(); }
		[[nodiscard]] bool_t IsByteAligned() const noexcept { return bitCount % 8 == 0; }

	private:
		void_t Flush()
		{
			while (bitCount >= 8)
			{
				bytes.push_back(static_cast<byte_t>(accumulator & 0xFFu));
				accumulator >>= 8;
				bitCount -= 8;
			}
		}
	};
}
