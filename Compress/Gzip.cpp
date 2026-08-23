//
// Created by hscloud on 26. 8. 12.
//

#include "Compress/Gzip.h"

#include "Compress/Internal/Checksum.h"
#include "Compress/Internal/Encode.h"
#include "Compress/Internal/Inflate.h"

namespace ne::compress
{
	namespace
	{
		using R = CompressResult<std::vector<byte_t>>;

		constexpr byte_t GzipId1 = 0x1F;
		constexpr byte_t GzipId2 = 0x8B;
		constexpr byte_t DeflateMethod = 8;

		// RFC 1952 §2.3.1 FLG 비트.
		constexpr byte_t FlagHeaderCrc = 0x02; // FHCRC — 헤더 CRC16 이 뒤따른다
		constexpr byte_t FlagExtra = 0x04;     // FEXTRA — XLEN + 확장 필드
		constexpr byte_t FlagName = 0x08;      // FNAME — 널 종료 파일명
		constexpr byte_t FlagComment = 0x10;   // FCOMMENT — 널 종료 주석
		constexpr byte_t FlagReserved = 0xE0;  // 예약 비트 — 설정되어 있으면 우리가 모르는 형식이다

		constexpr std::size_t FixedHeaderSize = 10;
		constexpr std::size_t TrailerSize = 8; // CRC32(4) + ISIZE(4)

		/** @brief 널 종료 문자열을 건너뛴다. 종료 바이트를 찾지 못하면 실패. */
		[[nodiscard]] bool_t SkipNullTerminated(const std::span<const byte_t> _input, std::size_t& _offset) noexcept
		{
			while (_offset < _input.size())
			{
				if (_input[_offset++] == byte_t{ 0 }) return true;
			}

			return false;
		}
	}



	CompressResult<std::vector<byte_t>> GzipDecompress(const std::span<const byte_t> _input, const std::size_t _maxOutput)
	{
		if (_input.size() < FixedHeaderSize + TrailerSize) return R::Error(CompressError{ CompressErrorKind::TRUNCATED_STREAM, "gzip stream too short" }.Context("[Compress/GzipDecompress]"));

		if (_input[0] != GzipId1 || _input[1] != GzipId2) return R::Error(CompressError{ CompressErrorKind::MALFORMED_STREAM, "not a gzip stream (bad magic)" }.Context("[Compress/GzipDecompress]"));
		if (_input[2] != DeflateMethod) return R::Error(CompressError{ CompressErrorKind::MALFORMED_STREAM, "gzip compression method is not deflate" }.Context("[Compress/GzipDecompress]"));

		const byte_t flags = _input[3];

		// 예약 비트가 켜져 있으면 우리가 해석하지 못하는 확장이 쓰인 것이다 — 추측하지 않고 거부한다.
		if ((flags & FlagReserved) != 0) return R::Error(CompressError{ CompressErrorKind::MALFORMED_STREAM, "gzip reserved flag bits are set" }.Context("[Compress/GzipDecompress]"));

		// MTIME(4) / XFL(1) / OS(1) 은 정보성 필드라 검사하지 않는다.
		std::size_t offset = FixedHeaderSize;

		if ((flags & FlagExtra) != 0)
		{
			if (offset + 2 > _input.size()) return R::Error(CompressError{ CompressErrorKind::TRUNCATED_STREAM, "truncated gzip extra field length" }.Context("[Compress/GzipDecompress]"));

			const std::size_t extraLength = static_cast<std::size_t>(_input[offset]) | (static_cast<std::size_t>(_input[offset + 1]) << 8);
			offset += 2;

			if (offset + extraLength > _input.size()) return R::Error(CompressError{ CompressErrorKind::TRUNCATED_STREAM, "truncated gzip extra field" }.Context("[Compress/GzipDecompress]"));
			offset += extraLength;
		}

		if ((flags & FlagName) != 0 && !SkipNullTerminated(_input, offset)) return R::Error(CompressError{ CompressErrorKind::TRUNCATED_STREAM, "unterminated gzip file name" }.Context("[Compress/GzipDecompress]"));
		if ((flags & FlagComment) != 0 && !SkipNullTerminated(_input, offset)) return R::Error(CompressError{ CompressErrorKind::TRUNCATED_STREAM, "unterminated gzip comment" }.Context("[Compress/GzipDecompress]"));

		if ((flags & FlagHeaderCrc) != 0)
		{
			// 헤더 CRC16 은 검증하지 않고 건너뛴다 — 본문 CRC32 가 어차피 전체를 보증하고, 헤더 CRC 를
			// 틀리게 쓰는 구현이 실제로 있어서 여기서 거부하면 호환성만 잃는다.
			if (offset + 2 > _input.size()) return R::Error(CompressError{ CompressErrorKind::TRUNCATED_STREAM, "truncated gzip header crc" }.Context("[Compress/GzipDecompress]"));
			offset += 2;
		}

		if (offset + TrailerSize > _input.size()) return R::Error(CompressError{ CompressErrorKind::TRUNCATED_STREAM, "gzip has no deflate payload" }.Context("[Compress/GzipDecompress]"));

		const auto body = _input.subspan(offset);

		std::size_t consumed = 0;
		auto result = internal::Inflate(body, _maxOutput, consumed);
		if (result.IsError()) return R::Error(std::move(result.Error()).Context("[Compress/GzipDecompress]"));

		if (body.size() - consumed < TrailerSize) return R::Error(CompressError{ CompressErrorKind::TRUNCATED_STREAM, "missing gzip trailer" }.Context("[Compress/GzipDecompress]"));

		// 트레일러는 CRC32(4) + ISIZE(4), 둘 다 리틀엔디언.
		const auto trailer = body.subspan(consumed, TrailerSize);
		const uint_t expectedCrc = static_cast<uint_t>(trailer[0]) | (static_cast<uint_t>(trailer[1]) << 8) | (static_cast<uint_t>(trailer[2]) << 16) | (static_cast<uint_t>(trailer[3]) << 24);
		const uint_t expectedSize = static_cast<uint_t>(trailer[4]) | (static_cast<uint_t>(trailer[5]) << 8) | (static_cast<uint_t>(trailer[6]) << 16) | (static_cast<uint_t>(trailer[7]) << 24);

		if (internal::Crc32(result.Value()) != expectedCrc) return R::Error(CompressError{ CompressErrorKind::CHECKSUM_MISMATCH, "gzip crc32 mismatch" }.Context("[Compress/GzipDecompress]"));

		// ISIZE 는 2^32 모듈로라 4GB 이상에서는 자연히 어긋난다 — 그 크기는 상한에서 이미 걸린다.
		if (static_cast<uint_t>(result.Value().size() & 0xFFFFFFFFu) != expectedSize) return R::Error(CompressError{ CompressErrorKind::CHECKSUM_MISMATCH, "gzip isize mismatch" }.Context("[Compress/GzipDecompress]"));

		return R::Ok(std::move(result.Value()));
	}

	CompressResult<std::vector<byte_t>> GzipCompress(const std::span<const byte_t> _input, const int_t _level)
	{
		auto compressed = internal::Deflate(_input, _level);
		if (compressed.IsError()) return R::Error(std::move(compressed.Error()).Context("[Compress/GzipCompress]"));

		std::vector<byte_t> stream;
		stream.reserve(compressed.Value().size() + FixedHeaderSize + TrailerSize);

		stream.push_back(GzipId1);
		stream.push_back(GzipId2);
		stream.push_back(DeflateMethod);
		stream.push_back(0); // FLG — 파일명/주석/헤더CRC 없음
		for (int_t index = 0; index < 4; ++index) stream.push_back(0); // MTIME=0 (헤더 주석의 재현성 근거 참고)
		stream.push_back(0);   // XFL — 압축 레벨 힌트(정보성)
		stream.push_back(255); // OS — 255 = "알 수 없음". 실제 OS 를 적으면 같은 입력이 플랫폼마다 달라진다

		stream.insert(stream.end(), compressed.Value().begin(), compressed.Value().end());

		// 트레일러는 **원본** 의 CRC32 와 크기(2^32 모듈로), 둘 다 리틀엔디언.
		const uint_t crc = internal::Crc32(_input);
		for (int_t shift = 0; shift <= 24; shift += 8) stream.push_back(static_cast<byte_t>((crc >> shift) & 0xFFu));

		const uint_t size = static_cast<uint_t>(_input.size() & 0xFFFFFFFFu);
		for (int_t shift = 0; shift <= 24; shift += 8) stream.push_back(static_cast<byte_t>((size >> shift) & 0xFFu));

		return R::Ok(std::move(stream));
	}
}
