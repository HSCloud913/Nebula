//
// Created by hscloud on 26. 7. 24.
//

#pragma once
#include <chrono>
#include <stop_token>
#include <type_traits>
#include <utility>
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Io/Coroutine/Timeout.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"

namespace ne::network::http::internal
{
	/**
	 * @brief HTTP 요청 태스크에 데드라인을 씌우는 내부 공용 헬퍼입니다 — per-request 타임아웃의 뼈대.
	 *
	 * _makeTask 는 stop_token 을 받아 요청 Task<HttpResult<T>> 를 만드는 콜러블입니다. 시한 안에 끝나면
	 * 그 HttpResult 를 그대로, 시한을 넘기면 진행 중 요청을 넘겨준 stop_token 으로 취소하고
	 * HttpErrorKind::TIMEOUT 을 반환합니다. io::Timeout 의 optional 을 값 기반 HttpResult 로 바꿔 줍니다.
	 * 프로토콜 버전과 무관하므로(HTTP/1.1·HTTP/2 공용) http 의 internal 에 둡니다.
	 */
	template <typename MakeTask>
	[[nodiscard]] auto WithTimeout(ne::io::Context& _context, const std::chrono::milliseconds _timeout, MakeTask _makeTask)
		-> ne::Task<typename ne::io::TaskValueType<std::invoke_result_t<MakeTask, std::stop_token>>::type>
	{
		using Result_ = typename ne::io::TaskValueType<std::invoke_result_t<MakeTask, std::stop_token>>::type; // = HttpResult<T>

		auto outcome = co_await ne::io::Timeout(_context, _timeout, std::move(_makeTask));
		if (!outcome.has_value()) co_return Result_::Error(HttpError(HttpErrorKind::TIMEOUT).Context("[Http/WithTimeout]"));

		co_return std::move(*outcome);
	}
}
