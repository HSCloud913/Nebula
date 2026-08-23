//
// Created by hscloud on 26. 8. 23.
//

#include "Network/Protocol/Http/Internal/ContentEncoding.h"

#include <string>
#include <utility>
#include <vector>
#include "Compress/Codec.h"
#include "Util/Ascii.h"

namespace ne::network::http::internal
{
	namespace
	{
		// 헤더 토큰은 대소문자를 구분하지 않는다(RFC 9110 §8.4.1) — 비교 전에 여기서 정규화한다.
		[[nodiscard]] string_t NormalizeToken(const string_view_t _token)
		{
			string_t lowered;
			lowered.reserve(_token.size());

			for (const char_t character : _token)
			{
				if (ne::util::Ascii::IsSpace(character)) continue; // 앞뒤 공백 제거를 겸한다(토큰 내부에는 공백이 올 수 없다)

				lowered.push_back(static_cast<char_t>(ne::util::Ascii::Lower(character)));
			}

			return lowered;
		}

		// Body 는 소유 벡터일 수도, 외부 버퍼를 가리키는 BufferChain 일 수도 있다. 해제기는 연속된
		// 바이트를 요구하므로 어느 쪽이든 한 벡터로 모은다(압축 본문이라 원본보다 작은 쪽이다).
		[[nodiscard]] std::vector<byte_t> Flatten(const Body& _body)
		{
			std::vector<byte_t> bytes;
			bytes.reserve(_body.Size());

			// View() 는 BufferChain 을 **값으로** 돌려준다. range-for 의 범위식에 그대로 쓰면 Segments() 가
			// 참조하는 임시가 루프 본문 전에 파괴된다 — C++23 의 임시 수명 연장(P2718R0)을 구현한 GCC 에서는
			// 우연히 동작하고 MSVC 에서는 빈 결과가 나온다. 그래서 체인을 반드시 지역 변수로 붙든다.
			const ne::memory::BufferChain chain = _body.View();
			for (const auto& segment : chain.Segments()) bytes.insert(bytes.end(), segment.ptr, segment.ptr + segment.length);

			return bytes;
		}
	}



	bool_t ApplyAcceptEncoding(Request& _request)
	{
		if (_request.headers.Has("Accept-Encoding")) return false;

		_request.headers.Set("Accept-Encoding", ne::compress::AcceptEncodingHeader());

		return true;
	}

	HttpResult<void_t> DecodeResponseBody(Response& _response, const bool_t _isAutomatic)
	{
		using R = HttpResult<void_t>;

		// 우리가 광고하지 않았다면 압축된 바이트 자체가 호출자의 목적일 수 있다 — 손대지 않는다.
		if (!_isAutomatic) return R::Ok();

		const auto header = _response.headers.Get("Content-Encoding");
		if (!header.has_value()) return R::Ok();

		// 값이 여럿 겹친 경우(예: "gzip, br")는 다루지 않는다. 실사용에서 사실상 없고, 잘못 추측해
		// 절반만 풀면 조용히 깨진 본문을 넘기게 된다 — 압축된 채로 그대로 두고 헤더도 남긴다.
		const string_t token = NormalizeToken(*header);
		if (token.find(',') != string_t::npos) return R::Ok();

		const auto encoding = ne::compress::EncodingFromToken(token);
		if (!encoding.has_value()) return R::Ok(); // 모르는 토큰 — 우리가 요청하지 않았을 테니 그대로 둔다
		if (*encoding == ne::compress::Encoding::IDENTITY) return R::Ok();

		if (!ne::compress::IsSupported(*encoding)) return R::Error(HttpError(HttpErrorKind::MALFORMED_MESSAGE, string_t{ "server sent unsupported Content-Encoding: " } + token).Context("[Http/Decode]"));

		const std::vector<byte_t> compressed = Flatten(_response.body);

		auto decoded = ne::compress::Decode(*encoding, compressed);
		if (decoded.IsError())
		{
			// 압축 폭탄 상한 초과는 "본문이 너무 크다" 로 올린다 — 호출자가 이미 아는 어휘다.
			const bool_t isTooLarge = decoded.Error().Kind() == ne::compress::CompressErrorKind::OUTPUT_LIMIT_EXCEEDED;

			return R::Error(HttpError(isTooLarge ? HttpErrorKind::BODY_TOO_LARGE : HttpErrorKind::MALFORMED_MESSAGE, decoded.Error().What()).Context("[Http/Decode]"));
		}

		const std::size_t decodedSize = decoded.Value().size();
		_response.body = Body(std::move(decoded.Value()));

		// 본문은 평문이 됐다 — 헤더가 계속 압축이라고 말하면 이 응답을 다시 내보내는 코드가 깨진다.
		_response.headers.Remove("Content-Encoding");
		if (_response.headers.Has("Content-Length")) _response.headers.Set("Content-Length", std::to_string(decodedSize));

		return R::Ok();
	}

	void_t CompressResponseBody(const Request& _request, Response& _response, const Compression& _compression)
	{
		// 1xx/204/304 는 본문을 가질 수 없다. 압축 헤더를 붙이면 그 자체로 형식 위반이 된다.
		if (_response.statusCode < 200 || _response.statusCode == 204 || _response.statusCode == 304) return;

		// 스트리밍 본문은 크기를 모른다 — 압축하려면 전부 버퍼링해야 하고, 그러면 스트리밍을 쓴 이유가
		// 사라진다(청크 단위 압축은 별개의 기능이라 여기서 흉내내지 않는다).
		if (_response.body.IsStreaming()) return;

		// 핸들러가 직접 인코딩을 정했다면 그 판단을 존중한다(미리 압축해 둔 파일을 그대로 보내는 경우).
		if (_response.headers.Has("Content-Encoding")) return;

		if (_response.body.Size() < _compression.minimumBytes) return;

		// 허용 목록이 비어 있으면 전부 압축한다는 뜻이다.
		if (!_compression.contentTypes.empty())
		{
			const auto contentType = _response.headers.Get("Content-Type");
			if (!contentType.has_value()) return; // 타입을 모르면 이미 압축된 바이너리일 수 있다 — 건드리지 않는다

			bool_t isAllowed = false;
			for (const string_t& prefix : _compression.contentTypes) isAllowed = isAllowed || contentType->starts_with(prefix);

			if (!isAllowed) return;
		}

		const auto accept = _request.headers.Get("Accept-Encoding");
		if (!accept.has_value()) return;

		const ne::compress::Encoding encoding = ne::compress::SelectEncoding(*accept);
		if (encoding == ne::compress::Encoding::IDENTITY) return;

		const std::vector<byte_t> plain = Flatten(_response.body);

		auto compressed = ne::compress::Encode(encoding, plain, _compression.level);
		if (compressed.IsError()) return; // 압축은 최적화 — 실패하면 평문으로 보낸다(헤더 주석 참고)

		// 줄지 않았다면 압축하지 않은 것이 낫다. 클라이언트의 해제 비용까지 생각하면 더욱 그렇다.
		if (compressed.Value().size() >= plain.size()) return;

		const std::size_t compressedSize = compressed.Value().size();
		_response.body = Body(std::move(compressed.Value()));

		_response.headers.Set("Content-Encoding", string_t{ ne::compress::EncodingToToken(encoding) });
		_response.headers.Set("Content-Length", std::to_string(compressedSize));

		// 캐시에게 "이 응답은 Accept-Encoding 에 따라 달라진다" 고 알린다. 이것이 없으면 중간 캐시가
		// gzip 응답을 gzip 을 요청하지 않은 클라이언트에게 돌려줄 수 있다.
		if (const auto vary = _response.headers.Get("Vary"))
		{
			if (vary->find("Accept-Encoding") == string_view_t::npos && *vary != "*") _response.headers.Set("Vary", string_t{ *vary } + ", Accept-Encoding");
		}
		else
		{
			_response.headers.Set("Vary", "Accept-Encoding");
		}
	}
}
