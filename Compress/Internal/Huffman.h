//
// Created by hscloud on 26. 8. 12.
//

#pragma once
#include <array>
#include <span>
#include <vector>
#include "Base/Type.h"
#include "Compress/Internal/BitReader.h"

namespace ne::compress::internal
{
	/** @brief DEFLATE 가 허용하는 최대 코드 길이(RFC 1951). 이 값을 넘는 표는 형식 위반이다. */
	inline constexpr int_t MaxCodeBits = 15;

	/**
	 * @class HuffmanTable
	 * @brief 코드 길이 배열로부터 만든 정규 허프만(canonical Huffman) 복호 표입니다.
	 *
	 * 코드 자체를 저장하지 않고 **길이별 개수(count) + 길이순으로 정렬된 심볼(symbol)** 만 들고
	 * 있습니다. 정규 허프만은 "같은 길이의 코드는 심볼 순서대로 연속한 정수" 라는 성질이 있어서,
	 * 이 두 배열만으로 복호가 가능합니다(코드 표를 만들 필요도, 트리 노드를 할당할 필요도 없습니다).
	 *
	 * @note DEFLATE 와 HPACK 이 모두 정규 허프만을 쓰므로 이 표는 양쪽에서 쓸 수 있도록 형식에
	 * 중립적으로 두었습니다.
	 */
	class HuffmanTable
	{
	public:
		HuffmanTable() = default;

	private:
		// counts[len] = 길이가 len 인 코드의 개수(인덱스 0 은 "코드가 없는 심볼" 수).
		std::array<uint16_t, MaxCodeBits + 1> counts{};
		// 길이 오름차순, 같은 길이 안에서는 심볼 오름차순으로 나열된 심볼 목록.
		std::vector<uint16_t> symbols;

	public:
		/**
		 * @brief 심볼별 코드 길이 배열로 표를 만듭니다(길이 0 = 그 심볼은 쓰이지 않음).
		 *
		 * @return 성공 여부. 다음 두 경우를 형식 오류로 거부합니다:
		 *  - **over-subscribed**: 주어진 길이들로 접두 부호를 만들 수 없음(코드 공간 초과)
		 *  - **incomplete**: 코드 공간이 남음. 단, 심볼이 정확히 하나인 표는 예외로 허용합니다 —
		 *    거리 코드가 하나뿐인 스트림이 실제로 존재하며 zlib 도 이를 받아들입니다.
		 *
		 * @note 불완전한 표를 그냥 통과시키면, 정의되지 않은 코드를 만났을 때 복호기가 엉뚱한 심볼을
		 * 돌려주며 조용히 잘못된 데이터를 만듭니다. 그래서 표를 만드는 시점에 막습니다.
		 */
		[[nodiscard]] bool_t Build(std::span<const byte_t> _codeLengths);

		/**
		 * @brief 비트 스트림에서 코드 하나를 읽어 심볼을 복호합니다.
		 * @return 성공 여부. 입력 고갈 또는 정의되지 않은 코드면 false.
		 * @note 비트를 한 개씩 읽으며 길이별로 범위를 좁혀 갑니다. 테이블 룩업 방식보다 느리지만
		 * 표 구성 비용이 0 이고 코드가 짧습니다 — 최적화는 필요해진 뒤에 합니다.
		 */
		[[nodiscard]] bool_t Decode(BitReader& _reader, uint16_t& _symbol) const;

		/** @brief 표가 비어 있는지(쓰이는 심볼이 하나도 없는지). */
		[[nodiscard]] bool_t IsEmpty() const noexcept { return symbols.empty(); }
	};

	/** @brief RFC 1951 §3.2.6 이 정한 고정 허프만 표(리터럴/길이). 매 호출 재구성하지 않도록 공유한다. */
	[[nodiscard]] const HuffmanTable& FixedLiteralTable();

	/** @brief RFC 1951 §3.2.6 이 정한 고정 허프만 표(거리). */
	[[nodiscard]] const HuffmanTable& FixedDistanceTable();
}
