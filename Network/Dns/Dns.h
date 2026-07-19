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
#include "Io/IoResult.h"

BEGIN_NS(ne::network::dns)
	struct Candidate
	{
		int_t family{};
		string_t ip;
	};

	/** @brief _host 가 IP 리터럴(v4/v6)이면 파싱만으로, 호스트명이면 DNS 조회로 후보 전부(A/AAAA)를 돌려준다. */
	[[nodiscard]] ne::Task<ne::io::IoResult<std::vector<Candidate>>> Resolve(string_view_t _host);

END_NS
