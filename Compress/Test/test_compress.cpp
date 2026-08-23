//
// Created by hscloud on 26. 8. 23.
//
// 압축기(deflate/gzip/zlib) 검증.
//
// 자기 해제기로만 왕복시키는 테스트는 압축기와 해제기가 **같이** 틀렸을 때 통과합니다. 그래서 두
// 축으로 검증합니다. (1) 우리 해제기로 왕복 — 우리 해제기는 test_decompress.cpp 에서 실제 gzip(1)
// 출력으로 이미 검증됐으므로 독립적인 기준입니다. (2) 실제 gzip(1) 로 우리 출력을 풀어 보기 —
// 외부 구현이 받아들이는지가 최종 판정입니다(도구가 없는 환경에서는 건너뜁니다).

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Compress/Codec.h"
#include "Compress/Deflate.h"
#include "Compress/Gzip.h"

namespace
{
	using namespace ne;
	using namespace ne::compress;

	[[nodiscard]] std::vector<byte_t> Bytes(const std::string& _text) { return { reinterpret_cast<const byte_t*>(_text.data()), reinterpret_cast<const byte_t*>(_text.data()) + _text.size() }; }

	[[nodiscard]] std::string Text(const std::vector<byte_t>& _bytes) { return { reinterpret_cast<const char*>(_bytes.data()), _bytes.size() }; }

	/** @brief 반복이 많은 텍스트 — LZ77 매치와 동적 허프만이 둘 다 이득을 내는 입력. */
	[[nodiscard]] std::string RepetitiveText(const std::size_t _repeats)
	{
		std::string text;
		for (std::size_t index = 0; index < _repeats; ++index) text += "The quick brown fox jumps over the lazy dog. ";

		return text;
	}

	/**
	 * @brief 압축되지 않는 데이터 — stored 블록 선택과 "출력이 입력보다 커지지 않는다" 를 시험한다.
	 * @note rand() 대신 결정적 LCG 를 쓴다. 테스트가 실행마다 다른 입력을 보면 실패를 재현할 수 없다.
	 */
	[[nodiscard]] std::vector<byte_t> IncompressibleBytes(const std::size_t _size)
	{
		std::vector<byte_t> bytes;
		bytes.reserve(_size);

		uint_t state = 0x12345678u;
		for (std::size_t index = 0; index < _size; ++index)
		{
			state = state * 1103515245u + 12345u;
			bytes.push_back(static_cast<byte_t>((state >> 16) & 0xFFu));
		}

		return bytes;
	}

	/** @brief gzip(1) 이 이 환경에 있는지. 없으면 외부 검증 테스트를 건너뛴다. */
	[[nodiscard]] bool_t HasGzipTool()
	{
		// 표준 출력/에러를 버리고 종료 코드만 본다 — 있으면 0 을 돌려준다.
#if defined(_WIN32)
		return std::system("gzip --version > NUL 2>&1") == 0;
#else
		return std::system("gzip --version > /dev/null 2>&1") == 0;
#endif
	}

	/**
	 * @brief _compressed 를 파일로 쓰고 gzip(1) 로 무결성 검사(`gzip -t`)를 시킨다.
	 * @return gzip 이 이 스트림을 유효한 gzip 으로 받아들였는지.
	 */
	[[nodiscard]] bool_t GzipToolAccepts(const std::vector<byte_t>& _compressed, const std::string& _name)
	{
		const std::string path = "nebula_compress_test_" + _name + ".gz";

		std::FILE* file = std::fopen(path.c_str(), "wb");
		if (file == nullptr) return false;

		const std::size_t written = std::fwrite(_compressed.data(), 1, _compressed.size(), file);
		std::fclose(file);

		if (written != _compressed.size()) return false;

#if defined(_WIN32)
		const int_t status = std::system(("gzip -t \"" + path + "\" > NUL 2>&1").c_str());
#else
		const int_t status = std::system(("gzip -t '" + path + "' > /dev/null 2>&1").c_str());
#endif

		std::remove(path.c_str());

		return status == 0;
	}
}



// ───────────────────────── 우리 해제기로 왕복 ─────────────────────────

TEST(DeflateCompressTest, RoundTripsShortText)
{
	const auto original = Bytes("Hello, DEFLATE world!");

	const auto compressed = RawDeflate(original);
	ASSERT_TRUE(compressed.IsOk()) << compressed.Error().What();

	const auto restored = RawInflate(compressed.Value());
	ASSERT_TRUE(restored.IsOk()) << restored.Error().What();
	EXPECT_EQ(restored.Value(), original);
}

TEST(DeflateCompressTest, RoundTripsEmptyInput)
{
	// 빈 입력도 유효한 스트림이어야 한다 — BFINAL 이 켜진 블록이 하나는 있어야 하고, 그것이 없으면
	// 해제기가 입력을 더 기다리며 "잘렸다" 고 보고한다.
	const std::vector<byte_t> original;

	const auto compressed = RawDeflate(original);
	ASSERT_TRUE(compressed.IsOk()) << compressed.Error().What();
	ASSERT_FALSE(compressed.Value().empty());

	const auto restored = RawInflate(compressed.Value());
	ASSERT_TRUE(restored.IsOk()) << restored.Error().What();
	EXPECT_TRUE(restored.Value().empty());
}

TEST(DeflateCompressTest, RoundTripsSingleByte)
{
	const auto original = Bytes("x");

	const auto compressed = RawDeflate(original);
	ASSERT_TRUE(compressed.IsOk()) << compressed.Error().What();

	const auto restored = RawInflate(compressed.Value());
	ASSERT_TRUE(restored.IsOk()) << restored.Error().What();
	EXPECT_EQ(restored.Value(), original);
}

TEST(DeflateCompressTest, RoundTripsAtEveryLevel)
{
	const auto original = Bytes(RepetitiveText(200));

	for (int_t level = 0; level <= 9; ++level)
	{
		const auto compressed = RawDeflate(original, level);
		ASSERT_TRUE(compressed.IsOk()) << "level " << level << ": " << compressed.Error().What();

		const auto restored = RawInflate(compressed.Value());
		ASSERT_TRUE(restored.IsOk()) << "level " << level << ": " << restored.Error().What();
		EXPECT_EQ(restored.Value(), original) << "level " << level;
	}
}

TEST(DeflateCompressTest, RejectsLevelOutsideRange)
{
	const auto original = Bytes("data");

	EXPECT_EQ(RawDeflate(original, -1).Error().Kind(), CompressErrorKind::INVALID_ARGUMENT);
	EXPECT_EQ(RawDeflate(original, 10).Error().Kind(), CompressErrorKind::INVALID_ARGUMENT);
}

TEST(DeflateCompressTest, RoundTripsDataLargerThanOneBlock)
{
	// 블록 경계를 넘겨 여러 블록이 나오게 한다 — 블록마다 표를 새로 만들므로 경계 처리가 틀리면
	// 첫 블록만 정상이고 그 뒤가 깨진다.
	const auto original = Bytes(RepetitiveText(4000)); // 180KB

	const auto compressed = RawDeflate(original);
	ASSERT_TRUE(compressed.IsOk()) << compressed.Error().What();

	const auto restored = RawInflate(compressed.Value(), 1024 * 1024);
	ASSERT_TRUE(restored.IsOk()) << restored.Error().What();
	EXPECT_EQ(restored.Value(), original);
}

TEST(DeflateCompressTest, RoundTripsIncompressibleData)
{
	const auto original = IncompressibleBytes(70000); // stored 블록 상한(65535)을 넘겨 분할까지 시험한다

	const auto compressed = RawDeflate(original);
	ASSERT_TRUE(compressed.IsOk()) << compressed.Error().What();

	const auto restored = RawInflate(compressed.Value(), 1024 * 1024);
	ASSERT_TRUE(restored.IsOk()) << restored.Error().What();
	EXPECT_EQ(restored.Value(), original);
}

TEST(DeflateCompressTest, DoesNotMeaningfullyExpandIncompressibleData)
{
	// 이 보장이 없으면 서버 응답 압축을 켜는 것 자체가 위험해진다 — 이미 압축된 이미지/영상 응답이
	// 압축을 거치며 커지기 때문이다. 블록마다 stored 를 후보로 두는 이유가 여기에 있다.
	const auto original = IncompressibleBytes(65536);

	const auto compressed = RawDeflate(original);
	ASSERT_TRUE(compressed.IsOk()) << compressed.Error().What();

	// 블록 헤더 + LEN/NLEN 만큼의 여유(블록당 5바이트, 2블록)를 허용한다.
	EXPECT_LE(compressed.Value().size(), original.size() + 32) << "비압축 데이터가 의미 있게 커졌다";
}

TEST(DeflateCompressTest, ActuallyCompressesRepetitiveData)
{
	const auto original = Bytes(RepetitiveText(200)); // 9000 바이트

	const auto compressed = RawDeflate(original);
	ASSERT_TRUE(compressed.IsOk()) << compressed.Error().What();

	// gzip -9 는 이 입력을 89바이트(컨테이너 제외)로 만든다. 우리가 그만큼은 아니어도, 20:1 을 넘지
	// 못한다면 LZ77 또는 허프만 중 하나가 사실상 동작하지 않는다는 뜻이다.
	EXPECT_LT(compressed.Value().size() * 20, original.size()) << "압축률이 20:1 미만 — 매치 탐색이나 허프만이 동작하지 않는다";
}

TEST(DeflateCompressTest, HigherLevelIsNotWorse)
{
	const auto original = Bytes(RepetitiveText(500));

	const auto low = RawDeflate(original, 1);
	const auto high = RawDeflate(original, 9);
	ASSERT_TRUE(low.IsOk() && high.IsOk());

	EXPECT_LE(high.Value().size(), low.Value().size()) << "레벨을 올렸는데 결과가 커졌다 — 탐색 깊이 설정이 뒤집혔다";
}



// ───────────────────────── 컨테이너 ─────────────────────────

TEST(GzipCompressTest, RoundTripsThroughOwnDecompressor)
{
	const auto original = Bytes(RepetitiveText(100));

	const auto compressed = GzipCompress(original);
	ASSERT_TRUE(compressed.IsOk()) << compressed.Error().What();

	const auto restored = GzipDecompress(compressed.Value());
	ASSERT_TRUE(restored.IsOk()) << restored.Error().What();
	EXPECT_EQ(restored.Value(), original);
}

TEST(GzipCompressTest, IsDeterministic)
{
	// 같은 입력이 항상 같은 바이트여야 ETag 캐시 검증과 재현 가능한 빌드가 성립한다. MTIME 을 0 으로
	// 두는 이유가 이것이고, 시각을 넣으면 이 테스트가 실패한다.
	const auto original = Bytes(RepetitiveText(50));

	const auto first = GzipCompress(original);
	const auto second = GzipCompress(original);
	ASSERT_TRUE(first.IsOk() && second.IsOk());

	EXPECT_EQ(first.Value(), second.Value());
}

TEST(ZlibCompressTest, RoundTripsThroughOwnDecompressor)
{
	const auto original = Bytes(RepetitiveText(100));

	const auto compressed = ZlibCompress(original);
	ASSERT_TRUE(compressed.IsOk()) << compressed.Error().What();

	// 헤더 검사 비트가 틀리면 여기서 MALFORMED_STREAM 으로 즉시 걸린다.
	const auto restored = ZlibDecompress(compressed.Value());
	ASSERT_TRUE(restored.IsOk()) << restored.Error().What();
	EXPECT_EQ(restored.Value(), original);
}



// ───────────────────────── 외부 구현(gzip(1)) 이 우리 출력을 받아들이는가 ─────────────────────────

TEST(GzipCompressTest, RealGzipToolAcceptsOurOutput)
{
	if (!HasGzipTool()) GTEST_SKIP() << "gzip(1) 이 없어 외부 검증을 건너뜁니다";

	// 세 블록 형식을 모두 태운다: 반복 텍스트(동적), 짧은 텍스트(고정), 비압축 데이터(stored).
	const auto repetitive = Bytes(RepetitiveText(300));
	const auto shortText = Bytes("Hello, DEFLATE world!");
	const auto incompressible = IncompressibleBytes(4096);

	for (int_t level = 0; level <= 9; ++level)
	{
		const auto a = GzipCompress(repetitive, level);
		ASSERT_TRUE(a.IsOk());
		EXPECT_TRUE(GzipToolAccepts(a.Value(), "repetitive")) << "level " << level << ": gzip 이 우리 출력을 거부했다(반복 텍스트)";

		const auto b = GzipCompress(shortText, level);
		ASSERT_TRUE(b.IsOk());
		EXPECT_TRUE(GzipToolAccepts(b.Value(), "short")) << "level " << level << ": gzip 이 우리 출력을 거부했다(짧은 텍스트)";

		const auto c = GzipCompress(incompressible, level);
		ASSERT_TRUE(c.IsOk());
		EXPECT_TRUE(GzipToolAccepts(c.Value(), "incompressible")) << "level " << level << ": gzip 이 우리 출력을 거부했다(비압축 데이터)";
	}
}

TEST(GzipCompressTest, RealGzipToolAcceptsEmptyOutput)
{
	if (!HasGzipTool()) GTEST_SKIP() << "gzip(1) 이 없어 외부 검증을 건너뜁니다";

	const auto compressed = GzipCompress(std::vector<byte_t>{});
	ASSERT_TRUE(compressed.IsOk());

	EXPECT_TRUE(GzipToolAccepts(compressed.Value(), "empty"));
}



// ───────────────────────── Codec: 협상과 인코딩 ─────────────────────────

TEST(CodecEncodeTest, EncodesAndDecodesThroughDispatcher)
{
	const auto original = Bytes(RepetitiveText(80));

	for (const Encoding encoding : { Encoding::IDENTITY, Encoding::GZIP, Encoding::DEFLATE })
	{
		const auto encoded = Encode(encoding, original);
		ASSERT_TRUE(encoded.IsOk()) << EncodingToToken(encoding) << ": " << encoded.Error().What();

		const auto decoded = Decode(encoding, encoded.Value());
		ASSERT_TRUE(decoded.IsOk()) << EncodingToToken(encoding) << ": " << decoded.Error().What();
		EXPECT_EQ(decoded.Value(), original) << EncodingToToken(encoding);
	}
}

TEST(CodecEncodeTest, RefusesUnimplementedEncodings)
{
	EXPECT_EQ(Encode(Encoding::BROTLI, Bytes("data")).Error().Kind(), CompressErrorKind::UNSUPPORTED_ENCODING);
	EXPECT_EQ(Encode(Encoding::ZSTD, Bytes("data")).Error().Kind(), CompressErrorKind::UNSUPPORTED_ENCODING);
}

TEST(CodecSelectTest, PicksGzipWhenOffered)
{
	EXPECT_EQ(SelectEncoding("gzip, deflate"), Encoding::GZIP);
	EXPECT_EQ(SelectEncoding("gzip"), Encoding::GZIP);
	EXPECT_EQ(SelectEncoding("deflate, gzip"), Encoding::GZIP) << "헤더 순서가 아니라 우리 선호를 따른다";
}

TEST(CodecSelectTest, FallsBackToDeflateWhenGzipAbsent)
{
	EXPECT_EQ(SelectEncoding("deflate"), Encoding::DEFLATE);
	EXPECT_EQ(SelectEncoding("br, deflate, zstd"), Encoding::DEFLATE) << "우리가 만들 수 없는 것은 후보가 아니다";
}

TEST(CodecSelectTest, ReturnsIdentityWhenNothingUsable)
{
	EXPECT_EQ(SelectEncoding(""), Encoding::IDENTITY);
	EXPECT_EQ(SelectEncoding("br"), Encoding::IDENTITY);
	EXPECT_EQ(SelectEncoding("br, zstd"), Encoding::IDENTITY);
}

TEST(CodecSelectTest, HonoursExplicitRejection)
{
	// q=0 을 무시하면 클라이언트가 풀 수 없는 응답을 보내게 된다 — 조용히 깨지는 종류의 버그다.
	EXPECT_EQ(SelectEncoding("gzip;q=0"), Encoding::IDENTITY);
	EXPECT_EQ(SelectEncoding("gzip;q=0.0"), Encoding::IDENTITY);
	EXPECT_EQ(SelectEncoding("gzip;q=0, deflate"), Encoding::DEFLATE);
	EXPECT_EQ(SelectEncoding("gzip;q=0.5"), Encoding::GZIP) << "0 이 아닌 q 는 거부가 아니다";
}

TEST(CodecSelectTest, TreatsWildcardAsPermission)
{
	EXPECT_EQ(SelectEncoding("*"), Encoding::GZIP);
	EXPECT_EQ(SelectEncoding("br, *"), Encoding::GZIP);
	EXPECT_EQ(SelectEncoding("*;q=0"), Encoding::IDENTITY);
}

TEST(CodecSelectTest, IgnoresCaseAndWhitespace)
{
	EXPECT_EQ(SelectEncoding("  GZIP , deflate "), Encoding::GZIP);
	EXPECT_EQ(SelectEncoding("Deflate"), Encoding::DEFLATE);
}
