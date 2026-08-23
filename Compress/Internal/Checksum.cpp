//
// Created by hscloud on 26. 8. 12.
//

#include "Compress/Internal/Checksum.h"

#include <array>

namespace ne::compress::internal
{
	namespace
	{
		// CRC-32 표를 컴파일 타임에 만든다 — 런타임 초기화(정적 초기화 순서 문제)와 뮤텍스가 없다.
		constexpr std::array<uint_t, 256> MakeCrcTable() noexcept
		{
			constexpr uint_t Polynomial = 0xEDB88320u; // 반사(reflected) 표현

			std::array<uint_t, 256> table{};
			for (uint_t i = 0; i < 256; ++i)
			{
				uint_t value = i;
				for (int_t bit = 0; bit < 8; ++bit) value = (value & 1u) ? ((value >> 1) ^ Polynomial) : (value >> 1);

				table[i] = value;
			}

			return table;
		}

		constexpr auto CrcTable = MakeCrcTable();

		// Adler-32 의 모듈러스(65521 = 2^16 보다 작은 최대 소수, RFC 1950 이 지정).
		constexpr uint_t AdlerModulus = 65521u;

		// a/b 가 32비트를 넘기 전에 안전하게 누적할 수 있는 최대 바이트 수(RFC 1950 참고 구현과 동일).
		constexpr std::size_t AdlerBlockSize = 5552;
	}



	uint_t Crc32(const std::span<const byte_t> _data, const uint_t _seed) noexcept
	{
		// 규격은 초기값 0xFFFFFFFF 로 시작해 마지막에 반전한다. _seed 로 이어 계산할 수 있게 하려면
		// 호출자에게는 반전된 값을 주고 여기서 다시 반전해 내부 상태로 되돌린다.
		uint_t crc = ~_seed;

		for (const byte_t byte : _data) crc = CrcTable[(crc ^ static_cast<uint_t>(byte)) & 0xFFu] ^ (crc >> 8);

		return ~crc;
	}

	uint_t Adler32(const std::span<const byte_t> _data, const uint_t _seed) noexcept
	{
		uint_t a = _seed & 0xFFFFu;
		uint_t b = (_seed >> 16) & 0xFFFFu;

		std::size_t offset = 0;
		while (offset < _data.size())
		{
			// 블록 단위로 나눠 누적한 뒤 한 번만 모듈러 연산한다 — 바이트마다 % 를 하면 훨씬 느리다.
			const std::size_t blockEnd = offset + std::min<std::size_t>(AdlerBlockSize, _data.size() - offset);

			for (; offset < blockEnd; ++offset)
			{
				a += static_cast<uint_t>(_data[offset]);
				b += a;
			}

			a %= AdlerModulus;
			b %= AdlerModulus;
		}

		return (b << 16) | a;
	}
}
