//
// Created by hscloud on 26. 8. 12.
//

#include "Compress/Internal/Huffman.h"


namespace ne::compress::internal
{
	bool_t HuffmanTable::Build(const std::span<const byte_t> _codeLengths)
	{
		counts.fill(0);
		symbols.clear();

		for (const byte_t length : _codeLengths)
		{
			if (length > MaxCodeBits) return false;

			++counts[length];
		}

		// 쓰이는 심볼이 없는 표(예: 거리 코드를 전혀 쓰지 않는 블록)는 유효하다.
		if (counts[0] == _codeLengths.size()) return true;

		// 코드 공간 검사. 길이 1 부터 내려가며 남은 공간을 두 배로 늘리고 그 길이의 코드 수를 뺀다.
		// 음수가 되면 코드가 너무 많아 접두 부호가 성립하지 않는다(over-subscribed).
		int_t remaining = 1;
		for (int_t length = 1; length <= MaxCodeBits; ++length)
		{
			remaining <<= 1;
			remaining -= counts[length];

			if (remaining < 0) return false;
		}

		// remaining > 0 이면 코드 공간이 남는다(incomplete). 심볼이 정확히 하나인 표만 예외로 허용한다 —
		// 거리 코드가 하나뿐인 스트림이 현실에 존재하고 zlib 도 받아들인다. 그 외의 불완전한 표를
		// 통과시키면 정의되지 않은 코드에서 엉뚱한 심볼이 나와 조용히 데이터가 망가진다.
		if (remaining > 0)
		{
			const std::size_t usedSymbols = _codeLengths.size() - counts[0];
			if (usedSymbols != 1) return false;
		}

		// 길이별 시작 오프셋을 만들고, 심볼을 (길이, 심볼) 순서로 배치한다.
		std::array<uint16_t, MaxCodeBits + 2> offsets{};
		offsets[1] = 0;
		for (int_t length = 1; length <= MaxCodeBits; ++length) offsets[length + 1] = static_cast<uint16_t>(offsets[length] + counts[length]);

		symbols.resize(_codeLengths.size() - counts[0]);
		for (std::size_t symbol = 0; symbol < _codeLengths.size(); ++symbol)
		{
			const byte_t length = _codeLengths[symbol];
			if (length != 0) symbols[offsets[length]++] = static_cast<uint16_t>(symbol);
		}

		return true;
	}

	bool_t HuffmanTable::Decode(BitReader& _reader, uint16_t& _symbol) const
	{
		// 정규 허프만의 성질을 이용한다: 길이 len 의 코드들은 [first, first+count) 구간의 연속 정수다.
		// 비트를 하나씩 붙여 가며 그 구간에 들어오는 순간 심볼이 확정된다.
		int_t code = 0;
		int_t first = 0;
		int_t index = 0;

		for (int_t length = 1; length <= MaxCodeBits; ++length)
		{
			uint_t bit = 0;
			if (!_reader.ReadBit(bit)) return false;

			code |= static_cast<int_t>(bit);

			const int_t count = counts[length];
			if (code - first < count)
			{
				_symbol = symbols[static_cast<std::size_t>(index + (code - first))];
				return true;
			}

			index += count;
			first = (first + count) << 1;
			code <<= 1;
		}

		return false; // MaxCodeBits 를 넘겼다 = 정의되지 않은 코드
	}



	namespace
	{
		// RFC 1951 §3.2.6: 리터럴/길이 288개의 고정 코드 길이.
		//   0..143 → 8비트, 144..255 → 9비트, 256..279 → 7비트, 280..287 → 8비트
		[[nodiscard]] HuffmanTable MakeFixedLiteralTable()
		{
			std::array<byte_t, 288> lengths{};
			for (std::size_t symbol = 0; symbol < 144; ++symbol) lengths[symbol] = 8;
			for (std::size_t symbol = 144; symbol < 256; ++symbol) lengths[symbol] = 9;
			for (std::size_t symbol = 256; symbol < 280; ++symbol) lengths[symbol] = 7;
			for (std::size_t symbol = 280; symbol < 288; ++symbol) lengths[symbol] = 8;

			HuffmanTable table;
			(void_t)table.Build(lengths);

			return table;
		}

		// RFC 1951 §3.2.6: 거리 코드는 32개 전부 5비트다(30, 31 은 실제로 쓰이지 않지만 표에 존재해야
		// 코드 공간이 완전해진다 — 빼면 incomplete 로 거부된다).
		[[nodiscard]] HuffmanTable MakeFixedDistanceTable()
		{
			std::array<byte_t, 32> lengths{};
			lengths.fill(5);

			HuffmanTable table;
			(void_t)table.Build(lengths);

			return table;
		}
	}

	const HuffmanTable& FixedLiteralTable()
	{
		static const HuffmanTable table = MakeFixedLiteralTable();
		return table;
	}

	const HuffmanTable& FixedDistanceTable()
	{
		static const HuffmanTable table = MakeFixedDistanceTable();
		return table;
	}
}
