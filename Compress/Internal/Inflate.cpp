//
// Created by hscloud on 26. 8. 12.
//

#include "Compress/Internal/Inflate.h"

#include <array>
#include "Compress/Internal/BitReader.h"
#include "Compress/Internal/Huffman.h"
#include "Compress/Internal/Tables.h"

namespace ne::compress::internal
{
	namespace
	{
		using R = CompressResult<std::vector<byte_t>>;

		[[nodiscard]] CompressError Malformed(const string_view_t _detail) { return CompressError{ CompressErrorKind::MALFORMED_STREAM, _detail }; }
		[[nodiscard]] CompressError Truncated() { return CompressError{ CompressErrorKind::TRUNCATED_STREAM }; }


		/** @brief 압축되지 않은(stored) 블록을 그대로 복사한다. */
		[[nodiscard]] CompressResult<void_t> InflateStored(BitReader& _reader, std::vector<byte_t>& _output, const std::size_t _maxOutput)
		{
			using VoidResult = CompressResult<void_t>;

			_reader.AlignToByte();

			const auto header = _reader.ReadBytes(4);
			if (header.empty()) return VoidResult::Error(Truncated());

			const uint_t length = static_cast<uint_t>(header[0]) | (static_cast<uint_t>(header[1]) << 8);
			const uint_t inverseLength = static_cast<uint_t>(header[2]) | (static_cast<uint_t>(header[3]) << 8);

			// LEN 과 그 1의 보수가 맞지 않으면 스트림이 손상된 것이다 — 규격이 명시한 검사다.
			if ((length ^ 0xFFFFu) != inverseLength) return VoidResult::Error(Malformed("stored block length complement mismatch"));

			if (_output.size() + length > _maxOutput) return VoidResult::Error(CompressError{ CompressErrorKind::OUTPUT_LIMIT_EXCEEDED });

			const auto payload = _reader.ReadBytes(length);
			if (payload.size() != length) return VoidResult::Error(Truncated());

			_output.insert(_output.end(), payload.begin(), payload.end());

			return VoidResult::Ok();
		}

		/**
		 * @brief 동적 블록의 헤더를 읽어 리터럴/거리 허프만 표를 만든다(RFC 1951 §3.2.7).
		 *
		 * 코드 길이 자체가 허프만으로 압축되어 있어 표를 세 번 만든다: (1) 코드 길이 알파벳용 표,
		 * (2) 그것으로 복호한 길이들로 리터럴 표, (3) 같은 방식으로 거리 표.
		 */
		[[nodiscard]] CompressResult<void_t> ReadDynamicTables(BitReader& _reader, HuffmanTable& _literalTable, HuffmanTable& _distanceTable)
		{
			using VoidResult = CompressResult<void_t>;

			uint_t literalCount = 0;
			uint_t distanceCount = 0;
			uint_t codeLengthCount = 0;
			if (!_reader.Read(5, literalCount) || !_reader.Read(5, distanceCount) || !_reader.Read(4, codeLengthCount)) return VoidResult::Error(Truncated());

			literalCount += 257;
			distanceCount += 1;
			codeLengthCount += 4;

			if (literalCount > MaxLiteralCodes || distanceCount > MaxDistanceCodes) return VoidResult::Error(Malformed("too many literal/distance codes"));

			// (1) 코드 길이 알파벳의 길이를 지정된 순서로 읽는다(각 3비트). 생략된 뒤쪽은 0 이다.
			std::array<byte_t, 19> codeLengthLengths{};
			for (uint_t i = 0; i < codeLengthCount; ++i)
			{
				uint_t bits = 0;
				if (!_reader.Read(3, bits)) return VoidResult::Error(Truncated());

				codeLengthLengths[CodeLengthOrder[i]] = static_cast<byte_t>(bits);
			}

			HuffmanTable codeLengthTable;
			if (!codeLengthTable.Build(codeLengthLengths)) return VoidResult::Error(Malformed("invalid code-length table"));

			// (2)(3) 리터럴+거리 길이를 이어서 복호한다. 16/17/18 은 반복 명령이다.
			std::array<byte_t, MaxLiteralCodes + MaxDistanceCodes> lengths{};
			const std::size_t totalCount = literalCount + distanceCount;

			std::size_t index = 0;
			while (index < totalCount)
			{
				uint16_t symbol = 0;
				if (!codeLengthTable.Decode(_reader, symbol)) return VoidResult::Error(Truncated());

				if (symbol < 16)
				{
					lengths[index++] = static_cast<byte_t>(symbol);
					continue;
				}

				byte_t repeatValue = 0;
				uint_t repeatCount = 0;

				if (symbol == 16)
				{
					// 직전 길이를 3~6회 반복. 첫 항목에서 나오면 반복할 대상이 없으므로 형식 오류다.
					if (index == 0) return VoidResult::Error(Malformed("repeat-previous with no previous length"));

					uint_t extra = 0;
					if (!_reader.Read(2, extra)) return VoidResult::Error(Truncated());

					repeatValue = lengths[index - 1];
					repeatCount = 3 + extra;
				}
				else if (symbol == 17)
				{
					uint_t extra = 0;
					if (!_reader.Read(3, extra)) return VoidResult::Error(Truncated());

					repeatCount = 3 + extra;
				}
				else if (symbol == 18)
				{
					uint_t extra = 0;
					if (!_reader.Read(7, extra)) return VoidResult::Error(Truncated());

					repeatCount = 11 + extra;
				}
				else { return VoidResult::Error(Malformed("invalid code-length symbol")); }

				// 반복이 표 끝을 넘어가면 형식 오류다 — 넘치는 만큼 잘라 받아들이면 뒤이은 복호가 어긋난다.
				if (index + repeatCount > totalCount) return VoidResult::Error(Malformed("code-length repeat overruns table"));

				for (uint_t i = 0; i < repeatCount; ++i) lengths[index++] = repeatValue;
			}

			if (!_literalTable.Build(std::span<const byte_t>(lengths.data(), literalCount))) return VoidResult::Error(Malformed("invalid literal/length table"));
			if (!_distanceTable.Build(std::span<const byte_t>(lengths.data() + literalCount, distanceCount))) return VoidResult::Error(Malformed("invalid distance table"));

			return VoidResult::Ok();
		}

		/** @brief 허프만으로 압축된 블록(고정 또는 동적)을 해제한다. */
		[[nodiscard]] CompressResult<void_t> InflateCompressed(BitReader& _reader, std::vector<byte_t>& _output, const std::size_t _maxOutput, const HuffmanTable& _literalTable, const HuffmanTable& _distanceTable)
		{
			using VoidResult = CompressResult<void_t>;

			while (true)
			{
				uint16_t symbol = 0;
				if (!_literalTable.Decode(_reader, symbol)) return VoidResult::Error(Truncated());

				if (symbol < 256)
				{
					if (_output.size() >= _maxOutput) return VoidResult::Error(CompressError{ CompressErrorKind::OUTPUT_LIMIT_EXCEEDED });

					_output.push_back(static_cast<byte_t>(symbol));
					continue;
				}

				if (symbol == 256) return VoidResult::Ok(); // 블록 끝

				// 길이/거리 쌍 — 이미 출력한 데이터에서 복사한다.
				const std::size_t lengthIndex = symbol - 257u;
				if (lengthIndex >= LengthBase.size()) return VoidResult::Error(Malformed("invalid length symbol"));

				uint_t lengthExtra = 0;
				if (LengthExtraBits[lengthIndex] > 0 && !_reader.Read(LengthExtraBits[lengthIndex], lengthExtra)) return VoidResult::Error(Truncated());

				const std::size_t copyLength = LengthBase[lengthIndex] + lengthExtra;

				uint16_t distanceSymbol = 0;
				if (!_distanceTable.Decode(_reader, distanceSymbol)) return VoidResult::Error(Truncated());
				if (distanceSymbol >= DistanceBase.size()) return VoidResult::Error(Malformed("invalid distance symbol"));

				uint_t distanceExtra = 0;
				if (DistanceExtraBits[distanceSymbol] > 0 && !_reader.Read(DistanceExtraBits[distanceSymbol], distanceExtra)) return VoidResult::Error(Truncated());

				const std::size_t distance = DistanceBase[distanceSymbol] + distanceExtra;

				// 출력 시작보다 앞을 가리키는 거리는 형식 오류다. 검사하지 않으면 버퍼 앞을 읽는다.
				if (distance > _output.size()) return VoidResult::Error(Malformed("distance exceeds output produced so far"));
				if (_output.size() + copyLength > _maxOutput) return VoidResult::Error(CompressError{ CompressErrorKind::OUTPUT_LIMIT_EXCEEDED });

				// 겹치는 복사가 정상이다(예: 거리 1, 길이 100 = 같은 바이트 100개). 그래서 memcpy 가
				// 아니라 바이트 단위로 진행해야 한다 — 방금 쓴 바이트를 다시 읽는 것이 규격의 의도다.
				const std::size_t start = _output.size() - distance;
				for (std::size_t i = 0; i < copyLength; ++i) _output.push_back(_output[start + i]);
			}
		}
	}



	CompressResult<std::vector<byte_t>> Inflate(const std::span<const byte_t> _input, const std::size_t _maxOutput, std::size_t& _consumed)
	{
		BitReader reader{ _input };
		std::vector<byte_t> output;

		while (true)
		{
			uint_t isFinalBlock = 0;
			uint_t blockType = 0;
			if (!reader.Read(1, isFinalBlock) || !reader.Read(2, blockType)) return R::Error(Truncated());

			switch (blockType)
			{
				case 0: // stored
				{
					if (auto result = InflateStored(reader, output, _maxOutput); result.IsError()) return R::Error(std::move(result.Error()));
					break;
				}
				case 1: // 고정 허프만
				{
					if (auto result = InflateCompressed(reader, output, _maxOutput, FixedLiteralTable(), FixedDistanceTable()); result.IsError()) return R::Error(std::move(result.Error()));
					break;
				}
				case 2: // 동적 허프만
				{
					HuffmanTable literalTable;
					HuffmanTable distanceTable;
					if (auto tables = ReadDynamicTables(reader, literalTable, distanceTable); tables.IsError()) return R::Error(std::move(tables.Error()));

					if (auto result = InflateCompressed(reader, output, _maxOutput, literalTable, distanceTable); result.IsError()) return R::Error(std::move(result.Error()));
					break;
				}
				default: // 3 은 예약값 — 나타나면 손상된 스트림이다
					return R::Error(Malformed("reserved block type"));
			}

			if (isFinalBlock != 0) break;
		}

		// 마지막 블록의 남은 비트를 버려 바이트 경계를 맞춘다 — 호출자가 그 뒤의 트레일러를 읽는다.
		reader.AlignToByte();
		_consumed = reader.BytePosition();

		return R::Ok(std::move(output));
	}
}
