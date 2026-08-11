//
// Created by hscloud on 26. 7. 29.
//

#pragma once
#include <functional>
#include <vector>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Network/Protocol/Http/Message/Request.h"
#include "Network/Protocol/Http/Message/Response.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"
#include "Network/Protocol/Http/Message/Params.h"

namespace ne::network::http::internal
{
	/**
	 * @class Router
	 * @brief 버전 무관 HTTP 라우팅 테이블 — 패턴 매칭, 경로 파라미터 추출, 405/404 처리를 담당합니다(HTTP/1.1·HTTP/2 공용).
	 *
	 * 패턴은 '/' 세그먼트 단위로 매칭합니다:
	 *  - 리터럴 세그먼트: 정확 일치(디코딩 없음)
	 *  - "{name}": 비어 있지 않은 세그먼트 하나를 캡처(퍼센트 디코딩 적용)
	 *  - "{*name}": 남은 경로 전체를 캡처(빈 값 허용, 디코딩 없음) — 패턴의 마지막 세그먼트여야 하며 이후는 무시
	 *
	 * 등록 순서대로 첫 매치가 이깁니다. 쿼리스트링('?' 이후)은 매칭에서 제외합니다. 경로는 맞지만
	 * 메서드가 다르면 405(Allow 헤더), 아무것도 안 맞으면 NotFound 핸들러(미설정 시 404)로 응답합니다.
	 */
	class Router
	{
	public:
		using Handler = std::function<ne::Task<HttpResult<Response>>(const Request&)>;
		using RouteHandler = std::function<ne::Task<HttpResult<Response>>(const Request&, const PathParams&)>;

	private:
		struct Entry
		{
			Method method;
			std::vector<string_t> segments; // 패턴을 세그먼트로 미리 분해해 보관
			RouteHandler handler;
		};

		std::vector<Entry> routes;
		Handler notFound;

	public:
		void_t Add(Method _method, string_view_t _pattern, RouteHandler _handler);
		void_t SetNotFound(Handler _handler) { notFound = std::move(_handler); }

		/** @brief _request 를 등록된 라우트로 디스패치합니다(경로 파라미터 추출·405·NotFound 포함). */
		[[nodiscard]] ne::Task<HttpResult<Response>> Dispatch(const Request& _request) const;

	private:
		// _pattern(분해된 패턴 세그먼트)이 _path 와 매칭되면 true 를 반환하고 _params 에 캡처를 채운다.
		[[nodiscard]] static bool_t Match(const std::vector<string_t>& _pattern, string_view_t _path, PathParams& _params);
	};
}
