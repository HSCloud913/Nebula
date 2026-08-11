//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include <chrono>
#include <cstddef>
#include "Base/Type.h"

namespace ne::network::http
{
	/**
	 * @class Limits
	 * @brief 신뢰할 수 없는 피어가 자원을 고갈시키는 것을 막는 서버 상한 모음입니다.
	 *
	 * 서버는 ServerBuilder 로 조정합니다(HTTP/1.1·HTTP/2 공용). 크기 초과 시 HTTP/1.1 은 431/413 응답
	 * 후 연결 종료, HTTP/2 는 헤더 초과 시 연결 종료·본문 초과 시 해당 스트림만 RST_STREAM 으로 거부합니다.
	 *
	 * @note 시간 상한이 없으면 크기 상한만으로는 방어가 되지 않습니다 — 분당 1바이트를 보내는 연결은
	 * 어떤 크기 제한도 넘지 않으면서 코루틴 프레임과 버퍼를 영구히 점유합니다(slowloris). 그래서 헤더/
	 * 본문/유휴 데드라인과 동시 연결 수 상한을 함께 둡니다.
	 */
	struct Limits
	{
		std::size_t maxHeaderBytes{ 8 * 1024 };
		std::size_t maxBodyBytes{ 64 * 1024 * 1024 };

		/** @brief 요청 라인+헤더 전체가 도착하기까지 허용하는 시간. */
		std::chrono::milliseconds headerTimeout{ std::chrono::seconds(15) };
		/** @brief 본문 수신에 허용하는 시간. */
		std::chrono::milliseconds bodyTimeout{ std::chrono::seconds(30) };
		/** @brief keep-alive 연결에서 다음 요청이 시작되기까지 기다리는 시간. */
		std::chrono::milliseconds idleTimeout{ std::chrono::seconds(60) };

		/** @brief 한 keep-alive 연결에서 처리할 최대 요청 수(0 이면 무제한). */
		std::size_t maxRequestsPerConnection{ 1000 };
		/** @brief 서버가 동시에 유지할 최대 연결 수(0 이면 무제한). 초과분은 즉시 닫습니다. */
		std::size_t maxConnections{ 4096 };
		/** @brief HTTP/2 한 연결에서 동시에 열 수 있는 스트림 수(SETTINGS_MAX_CONCURRENT_STREAMS 로 광고). */
		std::uint32_t maxConcurrentStreams{ 128 };
	};
}
