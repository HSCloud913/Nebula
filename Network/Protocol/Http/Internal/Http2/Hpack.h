//
// Created by hscloud on 26. 7. 28.
//

#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>
#include "Base/Type.h"

namespace ne::network::http_2::internal
{
	/** @class HpackHeader @brief 헤더 필드 하나(이름/값). HTTP/2 에서 이름은 소문자여야 합니다(RFC 9113 §8.2.1). */
	struct HpackHeader
	{
		string_t name;
		string_t value;
	};

	using HeaderList = std::vector<HpackHeader>;

	// HPACK 동적 테이블 기본 최대 크기(RFC 7541 §4.2, SETTINGS_HEADER_TABLE_SIZE 기본값).
	inline constexpr std::size_t DefaultHeaderTableSize = 4096;

	/**
	 * @class HpackEncoder
	 * @brief 헤더 목록을 HPACK 헤더 블록으로 인코딩합니다.
	 *
	 * 구현을 단순·정확하게 유지하기 위해 모든 필드를 "인덱싱 없는 리터럴(literal without indexing)" +
	 * 비-Huffman 문자열로 내보냅니다 — 유효한 HPACK 이며, 피어 디코더의 동적 테이블을 키우지 않습니다.
	 * (수신 측 디코더는 피어가 보내는 인덱싱/Huffman 표현을 완전히 해석합니다.)
	 */
	class HpackEncoder
	{
	public:
		void_t Encode(const HeaderList& _headers, std::vector<byte_t>& _out) const;
	};

	/**
	 * @class HpackDecoder
	 * @brief HPACK 헤더 블록을 헤더 목록으로 디코딩합니다(정적/동적 테이블 + 정수/문자열 + Huffman 완전 지원).
	 *
	 * @note 동적 테이블 상태는 연결 수명 동안 유지되어야 하므로 연결당 하나의 디코더를 재사용합니다.
	 *       형식 오류(잘못된 인덱스/정수/Huffman)는 nullopt 로 보고합니다(COMPRESSION_ERROR 로 이어짐).
	 */
	class HpackDecoder
	{
	public:
		explicit HpackDecoder(const std::size_t _maxDynamicTableSize = DefaultHeaderTableSize) noexcept
			: maxDynamicSize(_maxDynamicTableSize) {}

	private:
		struct Entry
		{
			string_t name;
			string_t value;
			[[nodiscard]] std::size_t Size() const noexcept { return name.size() + value.size() + 32; } // RFC 7541 §4.1
		};

		std::deque<Entry> dynamicTable; // front = 가장 최근 삽입(인덱스 62)
		std::size_t dynamicSize{ 0 };
		std::size_t maxDynamicSize;

	public:
		[[nodiscard]] std::optional<HeaderList> Decode(std::span<const byte_t> _block);

	private:
		[[nodiscard]] bool_t Lookup(std::size_t _index, string_t& _name, string_t& _value) const;
		void_t AddToDynamicTable(string_t _name, string_t _value);
		void_t SetMaxDynamicSize(std::size_t _size);
	};

	/** @brief HPACK Huffman 코드(RFC 7541 Appendix B)로 인코딩된 바이트열을 디코딩합니다. 실패 시 nullopt. */
	[[nodiscard]] std::optional<std::vector<byte_t>> HuffmanDecode(std::span<const byte_t> _input);
}
