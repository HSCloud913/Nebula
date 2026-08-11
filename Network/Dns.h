//
// Created by hscloud on 26. 7. 11.
//
// 호스트명 -> IP 해석 전용 유틸리티. ne::io::Socket::Connect/Bind 는 숫자 IP 리터럴만 받으므로
// (엔진에 DNS 의존성을 두지 않기 위해), 상위(PlainStream 등)가 Connect 전에 이걸로 먼저 해석한다.
// IIoEngine 등 Io 레이어에 의존하지 않는 순수 유틸리티 — 어떤 스트림 구현에서도 재사용 가능.

#pragma once
#include <vector>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Base/Coroutine/IExecutor.h"
#include "Io/Diagnostic/Error.h"

namespace ne::network::dns
{
	/** @brief DNS 조회로 얻은 주소 후보 하나입니다(주소 체계와 IP 문자열). */
	struct Candidate
	{
		int_t family{};
		string_t ip;
	};

	/**
	 * @brief _host 가 IP 리터럴(v4/v6)이면 파싱만으로, 호스트명이면 DNS 조회로 후보 전부(A/AAAA)를 돌려준다.
	 *
	 * @param _executor 조회가 끝난 뒤 호출 코루틴을 재개할 루프(보통 ne::io::Context). getaddrinfo 는
	 * 블로킹이라 워커 스레드에서 실행되는데, 재개까지 그 스레드에서 하면 호출 코루틴의 나머지 전부가
	 * 루프 스레드를 벗어난다 — 단일 스레드를 전제하는 Io 엔진에는 치명적이다.
	 */
	[[nodiscard]] ne::Task<ne::io::IoResult<std::vector<Candidate>>> Resolve(string_view_t _host, ne::IExecutor& _executor);
}
