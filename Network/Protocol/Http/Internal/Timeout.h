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
	 * HttpErrorKind::TIMEOUT 을 반환합니다.
	 *
	 * io::Timeout 은 "탈락/성공" 을 optional 로 표현하는데(Io 계층은 HTTP 를 몰라야 하므로 옳다), HTTP
	 * 계층은 실패를 값으로 전파하는 HttpResult 를 쓴다. 이 함수는 그 두 어휘를 잇는 얇은 어댑터이며
	 * 경합 로직을 다시 구현하지 않는다. 프로토콜 버전과 무관하므로(HTTP/1.1·HTTP/2 공용) http 의
	 * internal 에 둔다.
	 *
	 * @note Deadline 을 받는 오버로드를 쓰면 여러 단계(헤더 읽기 → 본문 읽기 → 응답 쓰기)가 하나의
	 * 예산을 공유한다. 상대 지연 오버로드는 단일 단계에만 시한을 걸 때 쓴다.
	 */
	template <typename MakeTask>
	[[nodiscard]] auto WithTimeout(ne::io::Context& _context, const ne::time::Deadline _deadline, MakeTask _makeTask)
		-> ne::Task<typename ne::io::TaskValueType<std::invoke_result_t<MakeTask, std::stop_token>>::type>
	{
		using Result_ = typename ne::io::TaskValueType<std::invoke_result_t<MakeTask, std::stop_token>>::type; // = HttpResult<T>

		auto outcome = co_await ne::io::Timeout(_context, _deadline, std::move(_makeTask));
		if (!outcome.has_value()) co_return Result_::Error(HttpError(HttpErrorKind::TIMEOUT).Context("[Http/WithTimeout]"));

		co_return std::move(*outcome);
	}

	/** @brief 지금부터 _timeout 뒤를 시한으로 삼는 편의 오버로드. */
	template <typename MakeTask>
	[[nodiscard]] auto WithTimeout(ne::io::Context& _context, const std::chrono::milliseconds _timeout, MakeTask _makeTask)
		-> ne::Task<typename ne::io::TaskValueType<std::invoke_result_t<MakeTask, std::stop_token>>::type>
	{
		return WithTimeout(_context, _context.DeadlineAfter(_timeout), std::move(_makeTask));
	}
}
