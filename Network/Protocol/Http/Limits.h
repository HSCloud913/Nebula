//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include <cstddef>
#include "Base/Type.h"

namespace ne::network::http
{
	/**
	 * @class Limits
	 * @brief 수신 메시지 크기 상한 — 신뢰할 수 없는 피어가 무한정 큰 메시지로 메모리를 고갈시키는 것을 막습니다.
	 *
	 * 서버는 ServerBuilder 의 MaxHeaderBytes()/MaxBodyBytes() 로 조정합니다(HTTP/1.1·HTTP/2 공용).
	 * 초과 시 HTTP/1.1 은 431/413 응답 후 연결 종료, HTTP/2 는 헤더 초과 시 연결 종료·본문 초과 시 해당
	 * 스트림만 RST_STREAM 으로 거부합니다.
	 */
	struct Limits
	{
		std::size_t maxHeaderBytes{ 8 * 1024 };
		std::size_t maxBodyBytes{ 64 * 1024 * 1024 };
	};
}
