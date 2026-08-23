//
// Created by hscloud on 26. 8. 12.
//

#include "Compress/Deflate.h"

#include "Compress/Internal/Checksum.h"
#include "Compress/Internal/Encode.h"
#include "Compress/Internal/Inflate.h"

namespace ne::compress
{
	namespace
	{
		using R = CompressResult<std::vector<byte_t>>;
	}



	CompressResult<std::vector<byte_t>> RawInflate(const std::span<const byte_t> _input, const std::size_t _maxOutput)
	{
		std::size_t consumed = 0;
		auto result = internal::Inflate(_input, _maxOutput, consumed);
		if (result.IsError()) return R::Error(std::move(result.Error()).Context("[Compress/RawInflate]"));

		return R::Ok(std::move(result.Value()));
	}

	CompressResult<std::vector<byte_t>> ZlibDecompress(const std::span<const byte_t> _input, const std::size_t _maxOutput)
	{
		// RFC 1950 §2.2: CMF(1) + FLG(1) + [DICTID(4)] + DEFLATE + ADLER32(4)
		if (_input.size() < 6) return R::Error(CompressError{ CompressErrorKind::TRUNCATED_STREAM, "zlib stream too short" }.Context("[Compress/ZlibDecompress]"));

		const uint_t cmf = static_cast<uint_t>(_input[0]);
		const uint_t flg = static_cast<uint_t>(_input[1]);

		// 압축 방식은 8(deflate) 만 정의되어 있다.
		if ((cmf & 0x0Fu) != 8) return R::Error(CompressError{ CompressErrorKind::MALFORMED_STREAM, "zlib compression method is not deflate" }.Context("[Compress/ZlibDecompress]"));

		// 헤더 두 바이트를 빅엔디언 16비트로 본 값이 31 의 배수여야 한다 — 규격이 정한 무결성 검사다.
		if (((cmf << 8) | flg) % 31u != 0) return R::Error(CompressError{ CompressErrorKind::MALFORMED_STREAM, "zlib header check failed" }.Context("[Compress/ZlibDecompress]"));

		// FDICT: 미리 합의된 사전을 쓴다는 뜻인데, 그 사전을 전달할 경로가 없으므로 지원하지 않는다.
		if ((flg & 0x20u) != 0) return R::Error(CompressError{ CompressErrorKind::UNSUPPORTED_ENCODING, "zlib preset dictionary is not supported" }.Context("[Compress/ZlibDecompress]"));

		const auto body = _input.subspan(2);

		std::size_t consumed = 0;
		auto result = internal::Inflate(body, _maxOutput, consumed);
		if (result.IsError()) return R::Error(std::move(result.Error()).Context("[Compress/ZlibDecompress]"));

		// 트레일러는 Adler-32 4바이트(빅엔디언).
		if (body.size() - consumed < 4) return R::Error(CompressError{ CompressErrorKind::TRUNCATED_STREAM, "missing zlib adler32 trailer" }.Context("[Compress/ZlibDecompress]"));

		const auto trailer = body.subspan(consumed, 4);
		const uint_t expected = (static_cast<uint_t>(trailer[0]) << 24) | (static_cast<uint_t>(trailer[1]) << 16) | (static_cast<uint_t>(trailer[2]) << 8) | static_cast<uint_t>(trailer[3]);

		if (internal::Adler32(result.Value()) != expected) return R::Error(CompressError{ CompressErrorKind::CHECKSUM_MISMATCH, "zlib adler32 mismatch" }.Context("[Compress/ZlibDecompress]"));

		return R::Ok(std::move(result.Value()));
	}

	CompressResult<std::vector<byte_t>> RawDeflate(const std::span<const byte_t> _input, const int_t _level)
	{
		auto compressed = internal::Deflate(_input, _level);
		if (compressed.IsError()) return R::Error(std::move(compressed.Error()).Context("[Compress/RawDeflate]"));

		return R::Ok(std::move(compressed.Value()));
	}

	CompressResult<std::vector<byte_t>> ZlibCompress(const std::span<const byte_t> _input, const int_t _level)
	{
		auto compressed = internal::Deflate(_input, _level);
		if (compressed.IsError()) return R::Error(std::move(compressed.Error()).Context("[Compress/ZlibCompress]"));

		std::vector<byte_t> stream;
		stream.reserve(compressed.Value().size() + 6);

		// CMF: 하위 4비트 = 압축 방식(8=deflate), 상위 4비트 = log2(윈도우)-8. 우리는 32KB 를 쓰므로 7.
		constexpr byte_t Cmf = 0x78;

		// FLG 의 상위 2비트는 압축 레벨 힌트(정보성)이고, 하위 5비트는 (CMF<<8|FLG) % 31 == 0 이 되도록
		// 채우는 검사 비트다. 레벨 힌트를 0 으로 두면 FLG = 0x01 이 검사식을 만족한다.
		constexpr byte_t Flg = 0x01;
		static_assert(((Cmf << 8) | Flg) % 31 == 0, "zlib 헤더 검사 비트가 맞지 않는다 — 해제기가 즉시 거부한다");

		stream.push_back(Cmf);
		stream.push_back(Flg);
		stream.insert(stream.end(), compressed.Value().begin(), compressed.Value().end());

		// Adler-32 는 **원본** 에 대해 계산하고 빅엔디언으로 붙인다(gzip 의 CRC32 는 리틀엔디언 — 헷갈리기 쉽다).
		const uint_t adler = internal::Adler32(_input);
		for (int_t shift = 24; shift >= 0; shift -= 8) stream.push_back(static_cast<byte_t>((adler >> shift) & 0xFFu));

		return R::Ok(std::move(stream));
	}
}
