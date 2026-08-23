//
// Created by hscloud on 26. 8. 12.
//

#include "Compress/Codec.h"

#include <array>
#include "Compress/Gzip.h"
#include "Util/Ascii.h"

namespace ne::compress
{
	bool_t IsSupported(const Encoding _encoding) noexcept
	{
		switch (_encoding)
		{
			case Encoding::IDENTITY:
			case Encoding::GZIP:
			case Encoding::DEFLATE:
				return true;

			// 자체 구현하지 않는다 — Brotli 는 122KB 정적 사전과 컨텍스트 모델링, zstd 는 FSE 엔트로피
			// 코더가 필요해 검증 없는 손구현은 위험만 크다. 외부 백엔드를 붙이면 여기만 바뀐다.
			case Encoding::BROTLI:
			case Encoding::ZSTD:
				return false;
		}

		return false;
	}

	std::optional<Encoding> EncodingFromToken(const string_view_t _token) noexcept
	{
		// 토큰은 대소문자 구분이 없지만(RFC 9110 §8.4.1) 실사용은 전부 소문자다. 여기서는 정확 비교만
		// 하고, 대소문자 정규화는 헤더를 파싱하는 상위(HTTP 계층)가 맡는다.
		if (_token == "identity") return Encoding::IDENTITY;
		if (_token == "gzip" || _token == "x-gzip") return Encoding::GZIP;
		if (_token == "deflate") return Encoding::DEFLATE;
		if (_token == "br") return Encoding::BROTLI;
		if (_token == "zstd") return Encoding::ZSTD;

		return std::nullopt;
	}

	string_view_t EncodingToToken(const Encoding _encoding) noexcept
	{
		switch (_encoding)
		{
			case Encoding::IDENTITY: return "identity";
			case Encoding::GZIP:     return "gzip";
			case Encoding::DEFLATE:  return "deflate";
			case Encoding::BROTLI:   return "br";
			case Encoding::ZSTD:     return "zstd";
		}

		return "identity";
	}

	string_t AcceptEncodingHeader()
	{
		// gzip 을 먼저 둔다 — 서버가 품질값 없이 첫 번째를 고르는 구현이 흔하고, gzip 이 가장 널리
		// 검증된 경로다. `deflate` 는 zlib 컨테이너 없이 raw DEFLATE 를 보내는 서버가 있어(과거 IIS)
		// 함정이 있지만, 우리 해제기가 양쪽을 시도하므로 광고해도 안전하다.
		string_t header = "gzip, deflate";

		if (IsSupported(Encoding::BROTLI)) header += ", br";
		if (IsSupported(Encoding::ZSTD)) header += ", zstd";

		return header;
	}

	CompressResult<std::vector<byte_t>> Decode(const Encoding _encoding, const std::span<const byte_t> _input, const std::size_t _maxOutput)
	{
		using R = CompressResult<std::vector<byte_t>>;

		switch (_encoding)
		{
			case Encoding::IDENTITY:
			{
				if (_input.size() > _maxOutput) return R::Error(CompressError{ CompressErrorKind::OUTPUT_LIMIT_EXCEEDED }.Context("[Compress/Decode]"));

				return R::Ok(std::vector<byte_t>(_input.begin(), _input.end()));
			}
			case Encoding::GZIP:
				return GzipDecompress(_input, _maxOutput);

			case Encoding::DEFLATE:
			{
				// 규격은 zlib 컨테이너를 뜻하지만, 컨테이너 없이 raw DEFLATE 를 보내는 서버가 실제로
				// 있다(과거 IIS). 먼저 규격대로 해석하고 실패하면 raw 로 재시도한다 — 그 반대 순서로
				// 하면 raw 해석이 우연히 성공해 엉뚱한 데이터를 낼 수 있다(zlib 헤더 2바이트를 압축
				// 데이터로 오해하는 경우).
				auto asZlib = ZlibDecompress(_input, _maxOutput);
				if (asZlib.IsOk()) return asZlib;

				auto asRaw = RawInflate(_input, _maxOutput);
				if (asRaw.IsOk()) return asRaw;

				// 둘 다 실패하면 규격 경로의 에러를 돌려준다 — 그것이 원인에 더 가깝다.
				return R::Error(std::move(asZlib.Error()));
			}
			case Encoding::BROTLI:
			case Encoding::ZSTD:
				return R::Error(CompressError{ CompressErrorKind::UNSUPPORTED_ENCODING, EncodingToToken(_encoding) }.Context("[Compress/Decode]"));
		}

		return R::Error(CompressError{ CompressErrorKind::UNSUPPORTED_ENCODING }.Context("[Compress/Decode]"));
	}

	CompressResult<std::vector<byte_t>> Encode(const Encoding _encoding, const std::span<const byte_t> _input, const int_t _level)
	{
		using R = CompressResult<std::vector<byte_t>>;

		switch (_encoding)
		{
			case Encoding::IDENTITY:
				return R::Ok(std::vector<byte_t>(_input.begin(), _input.end()));

			case Encoding::GZIP:
				return GzipCompress(_input, _level);

			// 보낼 때는 규격대로 zlib 컨테이너를 쓴다 — raw 를 받아 주는 것은 해제기의 관용이지, 우리가
			// 규격을 어길 근거는 아니다.
			case Encoding::DEFLATE:
				return ZlibCompress(_input, _level);

			case Encoding::BROTLI:
			case Encoding::ZSTD:
				return R::Error(CompressError{ CompressErrorKind::UNSUPPORTED_ENCODING, EncodingToToken(_encoding) }.Context("[Compress/Encode]"));
		}

		return R::Error(CompressError{ CompressErrorKind::UNSUPPORTED_ENCODING }.Context("[Compress/Encode]"));
	}

	Encoding SelectEncoding(const string_view_t _acceptEncoding) noexcept
	{
		// 우리 선호 순서. gzip 을 먼저 두는 이유는 가장 널리 검증된 경로이기 때문이다.
		constexpr std::array<Encoding, 2> preference = { Encoding::GZIP, Encoding::DEFLATE };

		std::array<bool_t, preference.size()> isAllowed{};
		bool_t isWildcardAllowed = false;

		std::size_t position = 0;
		for (;;)
		{
			const std::size_t comma = _acceptEncoding.find(',', position);
			const string_view_t entry = _acceptEncoding.substr(position, comma == string_view_t::npos ? string_view_t::npos : comma - position);

			// 항목은 "토큰[;q=값]" 형태다. 세미콜론 뒤를 보지 않으면 q=0(거부)을 놓친다.
			const std::size_t semicolon = entry.find(';');
			const string_view_t rawToken = entry.substr(0, semicolon);
			const string_view_t parameters = semicolon == string_view_t::npos ? string_view_t{} : entry.substr(semicolon + 1);

			string_t token;
			for (const char_t character : rawToken)
			{
				if (!ne::util::Ascii::IsSpace(character)) token.push_back(static_cast<char_t>(ne::util::Ascii::Lower(character)));
			}

			// q=0 은 "이 인코딩은 쓰지 말라" 는 명시적 거부다. 0.0 / 0.000 도 같은 뜻이다.
			bool_t isRejected = false;
			if (const std::size_t quality = parameters.find("q="); quality != string_view_t::npos)
			{
				const string_view_t value = parameters.substr(quality + 2);
				isRejected = !value.empty() && value[0] == '0' && value.find_first_of("123456789") == string_view_t::npos;
			}

			if (!isRejected)
			{
				if (token == "*") isWildcardAllowed = true;

				for (std::size_t index = 0; index < preference.size(); ++index)
				{
					if (token == EncodingToToken(preference[index])) isAllowed[index] = true;
				}
			}

			if (comma == string_view_t::npos) break;
			position = comma + 1;
		}

		for (std::size_t index = 0; index < preference.size(); ++index)
		{
			if (isAllowed[index]) return preference[index];
		}

		// `*` 는 "명시하지 않은 것도 괜찮다" 는 뜻이므로 우리 1순위를 쓴다.
		if (isWildcardAllowed) return preference[0];

		return Encoding::IDENTITY;
	}
}
