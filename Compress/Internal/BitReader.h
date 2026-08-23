//
// Created by hscloud on 26. 8. 12.
//

#pragma once
#include <span>
#include "Base/Type.h"

namespace ne::compress::internal
{
	/**
	 * @class BitReader
	 * @brief DEFLATE 비트 스트림을 읽는 커서입니다.
	 *
	 * @note DEFLATE 는 비트 순서가 두 가지로 섞여 있습니다(RFC 1951 §3.1.1). 허프만 코드가 아닌
	 * 값(길이/거리의 추가 비트, 블록 헤더 등)은 **최하위 비트부터** 채워지고, 허프만 코드는 **최상위
	 * 비트부터** 채워집니다. 그래서 Read() 와 ReadBit() 의 조합 방식이 다릅니다 — Read() 는 누적
	 * 버퍼에서 하위 n비트를 떼어 그대로 쓰고, 허프만 디코더는 ReadBit() 결과를 왼쪽으로 밀어 넣으며
	 * 코드를 만듭니다. 이 비대칭이 DEFLATE 구현에서 가장 흔한 실수 지점입니다.
	 */
	class BitReader
	{
	public:
		explicit BitReader(const std::span<const byte_t> _input) noexcept
			: input(_input) {}

	private:
		std::span<const byte_t> input;
		std::size_t position{ 0 }; // 다음에 읽을 바이트 위치
		uint_t buffer{ 0 };        // 아직 소비하지 않은 비트(하위쪽이 먼저 소비된다)
		int_t bitCount{ 0 };       // buffer 에 남은 유효 비트 수

	public:
		/** @brief 입력이 고갈되어 더 채울 수 없는 상태인지(현재 버퍼에 남은 비트는 별개). */
		[[nodiscard]] bool_t IsExhausted() const noexcept { return position >= input.size() && bitCount == 0; }

		/**
		 * @brief 하위 비트부터 _count 비트를 읽습니다(허프만 코드가 **아닌** 값 전용).
		 * @return 읽은 값. 입력이 부족하면 nullopt.
		 * @note _count 는 0~24 를 전제합니다 — 그보다 크면 32비트 누적 버퍼가 넘칩니다.
		 */
		[[nodiscard]] bool_t Read(const int_t _count, uint_t& _out) noexcept
		{
			while (bitCount < _count)
			{
				if (position >= input.size()) return false;

				buffer |= static_cast<uint_t>(input[position++]) << bitCount;
				bitCount += 8;
			}

			_out = buffer & ((1u << _count) - 1u);
			buffer >>= _count;
			bitCount -= _count;

			return true;
		}

		/** @brief 1비트를 읽습니다(허프만 디코딩의 기본 단위). 입력 고갈 시 false. */
		[[nodiscard]] bool_t ReadBit(uint_t& _out) noexcept { return Read(1, _out); }

		/**
		 * @brief 남은 비트를 버려 바이트 경계로 맞춥니다(stored 블록의 LEN 을 읽기 전에 필요).
		 * @note 버려지는 비트는 규격상 의미가 없습니다 — 값을 검사하지 않습니다.
		 */
		void_t AlignToByte() noexcept
		{
			const int_t remainder = bitCount % 8;
			buffer >>= remainder;
			bitCount -= remainder;
		}

		/**
		 * @brief 바이트 경계에서 원시 바이트 _count 개를 직접 가져옵니다(stored 블록 본문).
		 * @return 가져온 구간. 입력이 부족하면 빈 span.
		 * @note AlignToByte() 후에 호출해야 합니다. 버퍼에 남은 완전 바이트가 있으면 그것부터 소비합니다.
		 */
		[[nodiscard]] std::span<const byte_t> ReadBytes(const std::size_t _count) noexcept
		{
			// AlignToByte() 이후 buffer 에 남은 것은 8의 배수 비트다 — 그 바이트들을 먼저 되돌려 놓는다.
			while (bitCount >= 8 && position > 0)
			{
				--position;
				bitCount -= 8;
			}
			buffer = 0;
			bitCount = 0;

			if (input.size() - position < _count) return {};

			const auto result = input.subspan(position, _count);
			position += _count;

			return result;
		}

		/** @brief 트레일러(gzip CRC/ISIZE, zlib Adler) 를 읽기 위해 소비하고 남은 바이트 위치를 알려줍니다. */
		[[nodiscard]] std::size_t BytePosition() const noexcept { return position - static_cast<std::size_t>(bitCount / 8); }
	};
}
