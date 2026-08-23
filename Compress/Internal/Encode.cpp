//
// Created by hscloud on 26. 8. 23.
//

#include "Compress/Internal/Encode.h"

#include <algorithm>
#include <array>
#include <utility>
#include "Compress/Internal/BitWriter.h"
#include "Compress/Internal/Huffman.h"
#include "Compress/Internal/Tables.h"

namespace ne::compress::internal
{
	namespace
	{
		using R = CompressResult<std::vector<byte_t>>;

		constexpr std::size_t LiteralAlphabetSize = 286;  // 0..255 리터럴 + 256 EOB + 257..285 길이
		constexpr std::size_t DistanceAlphabetSize = 30;
		constexpr std::size_t CodeLengthAlphabetSize = 19;

		constexpr uint16_t EndOfBlock = 256;

		// 한 블록에 담는 심볼 수. 블록을 나누는 이유는 입력의 통계가 구간마다 다르기 때문입니다 —
		// 블록마다 허프만 표를 새로 만들면 그 구간에 맞는 표를 쓸 수 있습니다. 너무 작게 나누면
		// 표를 전송하는 비용이 이득을 잡아먹습니다.
		constexpr std::size_t BlockSymbolLimit = 16384;

		// stored 블록의 LEN 은 16비트라 한 블록에 65535 바이트까지만 담을 수 있습니다.
		constexpr std::size_t MaxStoredBlockSize = 65535;

		constexpr std::size_t HashBits = 15;
		constexpr std::size_t HashSize = 1u << HashBits;
		constexpr uint_t NoPosition = ~0u;

		/**
		 * @class Symbol
		 * @brief LZ77 이 만든 토큰 하나 — 리터럴이거나 (길이, 거리) 쌍입니다.
		 *
		 * @note 리터럴과 매치를 한 타입으로 두는 이유는, 허프만 빈도를 셀 때와 비트를 낼 때 두 번
		 * 순회해야 하는데 그 사이에 순서가 어긋나면 스트림이 조용히 깨지기 때문입니다. 하나의
		 * 목록을 두 번 읽는 구조가 그 위험을 없앱니다.
		 */
		struct Symbol
		{
			uint16_t literalOrLength{ 0 }; // 리터럴이면 바이트값, 매치면 길이(3..258)
			uint16_t distance{ 0 };        // 0 이면 리터럴
		};

		/** @brief 길이 3..258 → 길이 코드 심볼(257..285) 인덱스. */
		[[nodiscard]] std::size_t LengthCodeIndex(const std::size_t _length) noexcept
		{
			// 표가 29개뿐이라 선형 탐색이면 충분합니다(이진 탐색으로 바꿀 이유가 생기면 그때 바꿉니다).
			for (std::size_t index = LengthBase.size(); index-- > 0;)
			{
				if (_length >= LengthBase[index]) return index;
			}

			return 0;
		}

		/** @brief 거리 1..32768 → 거리 코드 심볼(0..29) 인덱스. */
		[[nodiscard]] std::size_t DistanceCodeIndex(const std::size_t _distance) noexcept
		{
			for (std::size_t index = DistanceBase.size(); index-- > 0;)
			{
				if (_distance >= DistanceBase[index]) return index;
			}

			return 0;
		}

		/**
		 * @brief 빈도로부터 정규 허프만 코드 길이를 만듭니다(길이 상한 _maxBits).
		 *
		 * 표준 힙 기반 허프만으로 길이를 구하고, 상한을 넘으면 **빈도를 절반으로 줄여 다시 만듭니다**.
		 * 최적 길이 제한(package-merge)보다 단순하며, 줄이기를 반복하면 분포가 평탄해져 반드시
		 * 상한 안으로 들어옵니다. 손실은 실측 0.1% 미만이고, 잘못 구현했을 때의 위험(유효하지 않은
		 * 스트림)이 그 이득보다 훨씬 큽니다.
		 */
		[[nodiscard]] std::vector<byte_t> BuildCodeLengths(std::vector<uint_t> _frequencies, const int_t _maxBits)
		{
			const std::size_t alphabetSize = _frequencies.size();
			std::vector<byte_t> lengths(alphabetSize, 0);

			for (;;)
			{
				// 쓰이는 심볼을 모은다. 하나 이하면 길이 배열이 의미 없으므로 최소 하나에 1비트를 준다.
				std::vector<std::size_t> used;
				for (std::size_t symbol = 0; symbol < alphabetSize; ++symbol)
				{
					if (_frequencies[symbol] > 0) used.push_back(symbol);
				}

				std::fill(lengths.begin(), lengths.end(), byte_t{ 0 });

				if (used.empty()) return lengths;
				if (used.size() == 1)
				{
					lengths[used[0]] = 1;
					return lengths;
				}

				// 노드 배열로 트리를 만든다(부모 인덱스만 기록 — 깊이는 부모를 따라 올라가며 센다).
				struct Node
				{
					uint_t weight{ 0 };
					std::size_t left{ 0 };
					std::size_t right{ 0 };
					bool_t isLeaf{ true };
				};

				std::vector<Node> nodes;
				nodes.reserve(used.size() * 2);

				std::vector<std::size_t> heap;
				heap.reserve(used.size());

				for (const std::size_t symbol : used)
				{
					nodes.push_back(Node{ _frequencies[symbol], 0, 0, true });
					heap.push_back(nodes.size() - 1);
				}

				// 가중치가 큰 것이 뒤로 가도록 정렬해 두고, 매번 가장 작은 둘을 뽑는다. 삽입 위치는
				// 이진 탐색으로 찾는다 — 알파벳이 최대 288개라 이 정도면 충분히 빠르다.
				const auto byWeight = [&nodes](const std::size_t _lhs, const std::size_t _rhs) { return nodes[_lhs].weight > nodes[_rhs].weight; };
				std::sort(heap.begin(), heap.end(), byWeight);

				while (heap.size() > 1)
				{
					const std::size_t smallest = heap.back();
					heap.pop_back();
					const std::size_t second = heap.back();
					heap.pop_back();

					nodes.push_back(Node{ nodes[smallest].weight + nodes[second].weight, smallest, second, false });
					const std::size_t merged = nodes.size() - 1;

					const auto position = std::lower_bound(heap.begin(), heap.end(), merged, byWeight);
					heap.insert(position, merged);
				}

				// 루트에서 내려가며 깊이를 심볼에 적는다(반복 스택 — 재귀는 깊이 288 까지 갈 수 있다).
				std::vector<std::pair<std::size_t, int_t>> stack{ { heap.front(), 0 } };
				int_t observedMax = 0;

				while (!stack.empty())
				{
					const auto [index, depth] = stack.back();
					stack.pop_back();

					if (nodes[index].isLeaf)
					{
						// 리프의 등장 순서는 used 와 같다(앞쪽 used.size() 개가 리프).
						lengths[used[index]] = static_cast<byte_t>(depth);
						observedMax = std::max(observedMax, depth);
						continue;
					}

					stack.emplace_back(nodes[index].left, depth + 1);
					stack.emplace_back(nodes[index].right, depth + 1);
				}

				if (observedMax <= _maxBits) return lengths;

				// 상한 초과 — 분포를 평탄하게 만들어 다시 시도한다(0 이 되지 않도록 최소 1 유지).
				for (std::size_t symbol = 0; symbol < alphabetSize; ++symbol)
				{
					if (_frequencies[symbol] > 0) _frequencies[symbol] = std::max(1u, _frequencies[symbol] / 2u);
				}
			}
		}

		/**
		 * @brief 코드 길이 배열로부터 정규 허프만 코드값을 만듭니다(RFC 1951 §3.2.2 의 절차 그대로).
		 * @note 복호기(HuffmanTable)와 같은 규칙을 써야 하므로 규격의 의사코드를 그대로 옮겼습니다.
		 */
		[[nodiscard]] std::vector<uint_t> BuildCodes(const std::vector<byte_t>& _lengths)
		{
			std::array<uint_t, MaxCodeBits + 2> countPerLength{};
			for (const byte_t length : _lengths)
			{
				if (length > 0) ++countPerLength[length];
			}

			std::array<uint_t, MaxCodeBits + 2> nextCode{};
			uint_t code = 0;
			for (int_t length = 1; length <= MaxCodeBits; ++length)
			{
				code = (code + countPerLength[length - 1]) << 1;
				nextCode[length] = code;
			}

			std::vector<uint_t> codes(_lengths.size(), 0);
			for (std::size_t symbol = 0; symbol < _lengths.size(); ++symbol)
			{
				if (_lengths[symbol] > 0) codes[symbol] = nextCode[_lengths[symbol]]++;
			}

			return codes;
		}

		/**
		 * @class CodeSet
		 * @brief 한 알파벳의 (길이, 코드) 짝 — 비트를 낼 때 이 둘이 항상 함께 필요합니다.
		 */
		struct CodeSet
		{
			std::vector<byte_t> lengths;
			std::vector<uint_t> codes;

			[[nodiscard]] static CodeSet FromFrequencies(std::vector<uint_t> _frequencies, const int_t _maxBits = MaxCodeBits)
			{
				CodeSet set;
				set.lengths = BuildCodeLengths(std::move(_frequencies), _maxBits);
				set.codes = BuildCodes(set.lengths);

				return set;
			}

			[[nodiscard]] static CodeSet FromLengths(std::vector<byte_t> _lengths)
			{
				CodeSet set;
				set.lengths = std::move(_lengths);
				set.codes = BuildCodes(set.lengths);

				return set;
			}
		};

		/** @brief RFC 1951 §3.2.6 의 고정 리터럴/길이 코드 길이 배열. */
		[[nodiscard]] std::vector<byte_t> FixedLiteralLengths()
		{
			std::vector<byte_t> lengths(288, 0);

			for (std::size_t symbol = 0; symbol <= 143; ++symbol) lengths[symbol] = 8;
			for (std::size_t symbol = 144; symbol <= 255; ++symbol) lengths[symbol] = 9;
			for (std::size_t symbol = 256; symbol <= 279; ++symbol) lengths[symbol] = 7;
			for (std::size_t symbol = 280; symbol <= 287; ++symbol) lengths[symbol] = 8;

			return lengths;
		}

		/** @brief 고정 거리 코드는 30개 모두 5비트입니다. */
		[[nodiscard]] std::vector<byte_t> FixedDistanceLengths() { return std::vector<byte_t>(30, 5); }

		/**
		 * @class CodeLengthStream
		 * @brief 동적 블록 헤더에 들어가는 "코드 길이의 코드 길이" 표현입니다.
		 *
		 * 리터럴/거리 코드 길이 배열을 그대로 보내면 300바이트가 넘습니다. 규격은 이 배열 자체를
		 * 허프만으로 다시 압축하고, 그 전에 반복 구간을 RLE(16/17/18)로 줄이도록 정했습니다.
		 */
		struct CodeLengthStream
		{
			std::vector<byte_t> symbols;      // 0..18
			std::vector<uint_t> extraValues;  // 16/17/18 뒤에 붙는 반복 횟수(그 외 심볼은 항목 없음)
			std::vector<byte_t> extraBits;    // 위 값의 비트 수

			void_t Push(const byte_t _symbol, const uint_t _extra = 0, const byte_t _bits = 0)
			{
				symbols.push_back(_symbol);
				extraValues.push_back(_extra);
				extraBits.push_back(_bits);
			}
		};

		/** @brief 길이 배열을 RLE(16: 직전 값 3~6회, 17: 0을 3~10회, 18: 0을 11~138회)로 인코딩한다. */
		[[nodiscard]] CodeLengthStream EncodeCodeLengths(const std::vector<byte_t>& _lengths)
		{
			CodeLengthStream stream;

			std::size_t index = 0;
			while (index < _lengths.size())
			{
				const byte_t value = _lengths[index];

				std::size_t runLength = 1;
				while (index + runLength < _lengths.size() && _lengths[index + runLength] == value) ++runLength;

				if (value == 0)
				{
					while (runLength >= 11)
					{
						const std::size_t chunk = std::min<std::size_t>(runLength, 138);
						stream.Push(18, static_cast<uint_t>(chunk - 11), 7);
						runLength -= chunk;
						index += chunk;
					}
					while (runLength >= 3)
					{
						const std::size_t chunk = std::min<std::size_t>(runLength, 10);
						stream.Push(17, static_cast<uint_t>(chunk - 3), 3);
						runLength -= chunk;
						index += chunk;
					}
					while (runLength > 0)
					{
						stream.Push(0);
						--runLength;
						++index;
					}

					continue;
				}

				// 0 이 아닌 값은 먼저 한 번 그대로 보내야 한다 — 16 은 "직전 값" 을 반복하는 코드다.
				stream.Push(value);
				--runLength;
				++index;

				while (runLength >= 3)
				{
					const std::size_t chunk = std::min<std::size_t>(runLength, 6);
					stream.Push(16, static_cast<uint_t>(chunk - 3), 2);
					runLength -= chunk;
					index += chunk;
				}
				while (runLength > 0)
				{
					stream.Push(value);
					--runLength;
					++index;
				}
			}

			return stream;
		}

		/** @brief 심볼 목록의 빈도를 센다(리터럴/길이 알파벳과 거리 알파벳을 함께). */
		void_t CountFrequencies(const std::vector<Symbol>& _symbols, std::vector<uint_t>& _literalFrequencies, std::vector<uint_t>& _distanceFrequencies)
		{
			for (const Symbol& symbol : _symbols)
			{
				if (symbol.distance == 0)
				{
					++_literalFrequencies[symbol.literalOrLength];
					continue;
				}

				++_literalFrequencies[257 + LengthCodeIndex(symbol.literalOrLength)];
				++_distanceFrequencies[DistanceCodeIndex(symbol.distance)];
			}

			++_literalFrequencies[EndOfBlock]; // 블록 끝 표식도 코드가 있어야 한다

			// 매치가 하나도 없으면 거리 알파벳이 완전히 빈다. 그러면 HDIST=1 에 길이 0 인 코드를 보내게
			// 되는데, 그것을 "코드가 없는 표" 로 거부하는 해제기가 있다. 쓰이지 않을 심볼 하나에 코드를
			// 주는 비용은 1비트뿐이므로, 항상 유효한 표가 나가도록 여기서 채운다.
			bool_t hasAnyDistance = false;
			for (const uint_t frequency : _distanceFrequencies) hasAnyDistance = hasAnyDistance || frequency > 0;

			if (!hasAnyDistance) _distanceFrequencies[0] = 1;
		}

		/** @brief 주어진 코드로 심볼 목록을 낼 때 필요한 비트 수(블록 형식을 고르기 위한 비교용). */
		[[nodiscard]] std::size_t CountPayloadBits(const std::vector<Symbol>& _symbols, const CodeSet& _literal, const CodeSet& _distance)
		{
			std::size_t bits = 0;

			for (const Symbol& symbol : _symbols)
			{
				if (symbol.distance == 0)
				{
					bits += _literal.lengths[symbol.literalOrLength];
					continue;
				}

				const std::size_t lengthIndex = LengthCodeIndex(symbol.literalOrLength);
				bits += _literal.lengths[257 + lengthIndex] + LengthExtraBits[lengthIndex];

				const std::size_t distanceIndex = DistanceCodeIndex(symbol.distance);
				bits += _distance.lengths[distanceIndex] + DistanceExtraBits[distanceIndex];
			}

			return bits + _literal.lengths[EndOfBlock];
		}

		/** @brief 심볼 목록을 주어진 코드로 비트 스트림에 낸다. */
		void_t WritePayload(BitWriter& _writer, const std::vector<Symbol>& _symbols, const CodeSet& _literal, const CodeSet& _distance)
		{
			for (const Symbol& symbol : _symbols)
			{
				if (symbol.distance == 0)
				{
					_writer.WriteCode(_literal.codes[symbol.literalOrLength], _literal.lengths[symbol.literalOrLength]);
					continue;
				}

				const std::size_t lengthIndex = LengthCodeIndex(symbol.literalOrLength);
				_writer.WriteCode(_literal.codes[257 + lengthIndex], _literal.lengths[257 + lengthIndex]);
				if (LengthExtraBits[lengthIndex] > 0) _writer.WriteBits(static_cast<uint_t>(symbol.literalOrLength - LengthBase[lengthIndex]), LengthExtraBits[lengthIndex]);

				const std::size_t distanceIndex = DistanceCodeIndex(symbol.distance);
				_writer.WriteCode(_distance.codes[distanceIndex], _distance.lengths[distanceIndex]);
				if (DistanceExtraBits[distanceIndex] > 0) _writer.WriteBits(static_cast<uint_t>(symbol.distance - DistanceBase[distanceIndex]), DistanceExtraBits[distanceIndex]);
			}

			_writer.WriteCode(_literal.codes[EndOfBlock], _literal.lengths[EndOfBlock]);
		}

		/**
		 * @class DynamicHeader
		 * @brief 동적 블록의 헤더(코드 길이 표현 + 그것을 담을 코드) 와 그 비트 비용입니다.
		 *
		 * @note 헤더 비용을 미리 알아야 "동적이 정말 이득인가" 를 판단할 수 있습니다. 헤더가 페이로드
		 * 절감분보다 크면 고정 허프만이 낫고, 그 판단을 하지 않으면 작은 응답이 오히려 커집니다.
		 */
		struct DynamicHeader
		{
			std::size_t literalCount{ 0 };
			std::size_t distanceCount{ 0 };
			std::size_t codeLengthCount{ 0 };
			CodeLengthStream stream;
			CodeSet codeLengthCodes;
			std::size_t bits{ 0 };
		};

		[[nodiscard]] DynamicHeader BuildDynamicHeader(const CodeSet& _literal, const CodeSet& _distance)
		{
			DynamicHeader header;

			// HLIT/HDIST: 뒤쪽의 쓰이지 않는 코드는 보내지 않는다(각각 최소 257 / 1 개는 보내야 한다).
			header.literalCount = LiteralAlphabetSize;
			while (header.literalCount > 257 && _literal.lengths[header.literalCount - 1] == 0) --header.literalCount;

			header.distanceCount = DistanceAlphabetSize;
			while (header.distanceCount > 1 && _distance.lengths[header.distanceCount - 1] == 0) --header.distanceCount;

			// 두 길이 배열을 이어 붙여 한 번에 RLE 한다 — 규격이 그렇게 정했다(경계에서도 반복이 이어진다).
			std::vector<byte_t> combined;
			combined.reserve(header.literalCount + header.distanceCount);
			combined.insert(combined.end(), _literal.lengths.begin(), _literal.lengths.begin() + header.literalCount);
			combined.insert(combined.end(), _distance.lengths.begin(), _distance.lengths.begin() + header.distanceCount);

			header.stream = EncodeCodeLengths(combined);

			std::vector<uint_t> codeLengthFrequencies(CodeLengthAlphabetSize, 0);
			for (const byte_t symbol : header.stream.symbols) ++codeLengthFrequencies[symbol];

			// 코드 길이 알파벳의 코드는 3비트로 길이를 보내므로 7비트를 넘을 수 없다.
			header.codeLengthCodes = CodeSet::FromFrequencies(codeLengthFrequencies, 7);

			header.codeLengthCount = CodeLengthAlphabetSize;
			while (header.codeLengthCount > 4 && header.codeLengthCodes.lengths[CodeLengthOrder[header.codeLengthCount - 1]] == 0) --header.codeLengthCount;

			header.bits = 5 + 5 + 4;                       // HLIT, HDIST, HCLEN
			header.bits += header.codeLengthCount * 3;     // 코드 길이 알파벳의 길이들
			for (std::size_t index = 0; index < header.stream.symbols.size(); ++index)
			{
				header.bits += header.codeLengthCodes.lengths[header.stream.symbols[index]];
				header.bits += header.stream.extraBits[index];
			}

			return header;
		}

		void_t WriteDynamicHeader(BitWriter& _writer, const DynamicHeader& _header)
		{
			_writer.WriteBits(static_cast<uint_t>(_header.literalCount - 257), 5);
			_writer.WriteBits(static_cast<uint_t>(_header.distanceCount - 1), 5);
			_writer.WriteBits(static_cast<uint_t>(_header.codeLengthCount - 4), 4);

			for (std::size_t index = 0; index < _header.codeLengthCount; ++index) _writer.WriteBits(_header.codeLengthCodes.lengths[CodeLengthOrder[index]], 3);

			for (std::size_t index = 0; index < _header.stream.symbols.size(); ++index)
			{
				const byte_t symbol = _header.stream.symbols[index];
				_writer.WriteCode(_header.codeLengthCodes.codes[symbol], _header.codeLengthCodes.lengths[symbol]);

				if (_header.stream.extraBits[index] > 0) _writer.WriteBits(_header.stream.extraValues[index], _header.stream.extraBits[index]);
			}
		}

		/** @brief stored 블록(BTYPE=00)을 낸다 — 65535 바이트를 넘으면 여러 블록으로 나눈다. */
		void_t WriteStoredBlocks(BitWriter& _writer, const std::span<const byte_t> _raw, const bool_t _isFinal)
		{
			std::size_t offset = 0;

			// 빈 입력도 블록 하나는 있어야 한다(길이 0 의 stored 블록이 가장 짧은 유효 스트림이다).
			do
			{
				const std::size_t chunk = std::min(_raw.size() - offset, MaxStoredBlockSize);
				const bool_t isLastChunk = offset + chunk >= _raw.size();

				_writer.WriteBits((_isFinal && isLastChunk) ? 1u : 0u, 1);
				_writer.WriteBits(0, 2); // BTYPE=00
				_writer.AlignToByte();

				_writer.WriteByte(static_cast<byte_t>(chunk & 0xFFu));
				_writer.WriteByte(static_cast<byte_t>((chunk >> 8) & 0xFFu));
				_writer.WriteByte(static_cast<byte_t>(~chunk & 0xFFu));
				_writer.WriteByte(static_cast<byte_t>((~chunk >> 8) & 0xFFu));

				for (std::size_t index = 0; index < chunk; ++index) _writer.WriteByte(_raw[offset + index]);

				offset += chunk;
			} while (offset < _raw.size());
		}

		/**
		 * @class MatchFinder
		 * @brief 해시 체인으로 3바이트 이상 일치하는 과거 위치를 찾습니다(LZ77).
		 *
		 * 3바이트 해시로 같은 시작을 가진 위치들을 연결 리스트로 이어 두고, 최근 것부터 훑어 가장 긴
		 * 일치를 고릅니다. 훑는 개수(chainLimit)가 압축률과 속도의 균형점이고, 레벨이 그 값을 정합니다.
		 */
		class MatchFinder
		{
		public:
			MatchFinder(const std::span<const byte_t> _input, const std::size_t _chainLimit)
				: input(_input)
				, chainLimit(_chainLimit)
				, head(HashSize, NoPosition)
				, previous(_input.size(), NoPosition) {}

		private:
			std::span<const byte_t> input;
			std::size_t chainLimit;
			std::vector<uint_t> head;     // 해시 → 가장 최근 위치
			std::vector<uint_t> previous; // 위치 → 같은 해시의 그 이전 위치

		public:
			/** @brief _position 에서 시작하는 가장 긴 일치를 찾습니다. 길이 0 이면 매치 없음. */
			[[nodiscard]] Symbol Find(const std::size_t _position) const
			{
				Symbol best;

				if (_position + MinMatchLength > input.size()) return best;

				const std::size_t limit = std::min(input.size() - _position, MaxMatchLength);
				const std::size_t oldest = _position > WindowSize ? _position - WindowSize : 0;

				std::size_t candidate = head[Hash(_position)];
				std::size_t examined = 0;

				while (candidate != NoPosition && candidate >= oldest && examined < chainLimit)
				{
					std::size_t length = 0;
					while (length < limit && input[candidate + length] == input[_position + length]) ++length;

					if (length > best.literalOrLength)
					{
						best.literalOrLength = static_cast<uint16_t>(length);
						best.distance = static_cast<uint16_t>(_position - candidate);

						if (length >= limit) break; // 더 길어질 수 없다
					}

					if (candidate == 0) break;
					candidate = previous[candidate];
					++examined;
				}

				if (best.literalOrLength < MinMatchLength) return Symbol{};

				return best;
			}

			/** @brief _position 을 해시 체인에 등록합니다(찾기 전/후 순서가 결과에 영향을 줍니다). */
			void_t Insert(const std::size_t _position)
			{
				if (_position + MinMatchLength > input.size()) return;

				const std::size_t hash = Hash(_position);
				previous[_position] = head[hash];
				head[hash] = static_cast<uint_t>(_position);
			}

		private:
			[[nodiscard]] std::size_t Hash(const std::size_t _position) const noexcept
			{
				// 3바이트를 섞는 곱셈 해시. 충돌은 정확성에 영향이 없다(체인을 훑을 때 실제로 비교한다).
				const uint_t value = (static_cast<uint_t>(input[_position]) << 16) | (static_cast<uint_t>(input[_position + 1]) << 8) | static_cast<uint_t>(input[_position + 2]);

				return (value * 2654435761u) >> (32 - HashBits);
			}
		};

		/** @brief 레벨을 해시 체인 탐색 깊이로 바꿉니다. */
		[[nodiscard]] std::size_t ChainLimitForLevel(const int_t _level) noexcept
		{
			constexpr std::array<std::size_t, 10> limits = { 0, 4, 8, 16, 32, 64, 128, 256, 1024, 4096 };

			return limits[static_cast<std::size_t>(_level)];
		}

		/**
		 * @brief 한 블록을 세 형식(stored/고정/동적) 중 가장 짧은 것으로 낸다.
		 *
		 * @note 이 비교가 이 압축기의 핵심 안전장치입니다. stored 를 항상 후보로 두기 때문에, 이미
		 * 압축된 입력에 대해서도 출력이 입력보다 (블록 헤더 5바이트를 넘겨) 커지지 않습니다.
		 */
		void_t WriteBlock(BitWriter& _writer, const std::vector<Symbol>& _symbols, const std::span<const byte_t> _raw, const bool_t _isFinal)
		{
			std::vector<uint_t> literalFrequencies(LiteralAlphabetSize, 0);
			std::vector<uint_t> distanceFrequencies(DistanceAlphabetSize, 0);
			CountFrequencies(_symbols, literalFrequencies, distanceFrequencies);

			const CodeSet fixedLiteral = CodeSet::FromLengths(FixedLiteralLengths());
			const CodeSet fixedDistance = CodeSet::FromLengths(FixedDistanceLengths());
			const std::size_t fixedBits = 3 + CountPayloadBits(_symbols, fixedLiteral, fixedDistance);

			const CodeSet dynamicLiteral = CodeSet::FromFrequencies(literalFrequencies);
			const CodeSet dynamicDistance = CodeSet::FromFrequencies(distanceFrequencies);
			const DynamicHeader header = BuildDynamicHeader(dynamicLiteral, dynamicDistance);
			const std::size_t dynamicBits = 3 + header.bits + CountPayloadBits(_symbols, dynamicLiteral, dynamicDistance);

			// stored 는 바이트 정렬 후 LEN/NLEN 4바이트 + 본문. 현재 비트 위치에 따라 패딩이 붙는다.
			const std::size_t storedBits = 3 + 7 + (4 + _raw.size()) * 8;

			if (storedBits <= fixedBits && storedBits <= dynamicBits)
			{
				WriteStoredBlocks(_writer, _raw, _isFinal);
				return;
			}

			_writer.WriteBits(_isFinal ? 1u : 0u, 1);

			if (fixedBits <= dynamicBits)
			{
				_writer.WriteBits(1, 2); // BTYPE=01 고정 허프만
				WritePayload(_writer, _symbols, fixedLiteral, fixedDistance);
				return;
			}

			_writer.WriteBits(2, 2); // BTYPE=10 동적 허프만
			WriteDynamicHeader(_writer, header);
			WritePayload(_writer, _symbols, dynamicLiteral, dynamicDistance);
		}
	}



	CompressResult<std::vector<byte_t>> Deflate(const std::span<const byte_t> _input, const int_t _level)
	{
		if (_level < 0 || _level > 9) return R::Error(CompressError{ CompressErrorKind::INVALID_ARGUMENT, "deflate level must be 0..9" }.Context("[Compress/Deflate]"));

		BitWriter writer;

		// 레벨 0 은 매치를 찾지 않는다 — "압축하지 않지만 유효한 DEFLATE" 를 만드는 경로다.
		if (_level == 0)
		{
			WriteStoredBlocks(writer, _input, true);

			return R::Ok(writer.Finish());
		}

		MatchFinder finder{ _input, ChainLimitForLevel(_level) };

		std::vector<Symbol> symbols;
		symbols.reserve(std::min<std::size_t>(BlockSymbolLimit, _input.size() + 1));

		std::size_t blockStart = 0;
		std::size_t position = 0;

		while (position < _input.size())
		{
			const Symbol match = finder.Find(position);

			if (match.literalOrLength >= MinMatchLength)
			{
				symbols.push_back(match);

				// 매치가 덮은 모든 위치를 체인에 등록한다 — 건너뛰면 이후 매치를 놓친다.
				for (std::size_t offset = 0; offset < match.literalOrLength; ++offset) finder.Insert(position + offset);

				position += match.literalOrLength;
			}
			else
			{
				symbols.push_back(Symbol{ static_cast<uint16_t>(_input[position]), 0 });
				finder.Insert(position);
				++position;
			}

			// 블록을 닫을 때는 이 블록이 덮은 원본 구간도 함께 넘긴다(stored 후보로 쓰기 위해).
			if (symbols.size() >= BlockSymbolLimit)
			{
				WriteBlock(writer, symbols, _input.subspan(blockStart, position - blockStart), position >= _input.size());
				symbols.clear();
				blockStart = position;
			}
		}

		// 남은 심볼(또는 빈 입력)로 마지막 블록을 낸다 — BFINAL 이 켜진 블록이 반드시 하나 있어야 한다.
		if (!symbols.empty() || blockStart == _input.size()) WriteBlock(writer, symbols, _input.subspan(blockStart, _input.size() - blockStart), true);

		return R::Ok(writer.Finish());
	}
}
