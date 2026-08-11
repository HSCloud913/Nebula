//
// Created by hscloud on 26. 8. 11.
//

#pragma once
#include <chrono>
#include <optional>
#include "Base/Type.h"

namespace ne::time
{
	/**
	 * @brief system_clock 시각을 HTTP-date(IMF-fixdate)로 포맷합니다 — 예: "Sun, 06 Nov 1994 08:49:37 GMT".
	 *
	 * RFC 9110 §5.6.7 이 규정하는 고정 폭 형식이며, 항상 GMT 기준이고 영어 요일/월 약어를 씁니다.
	 * 로케일에 영향을 받으면 안 되므로 표 조회로 직접 만듭니다(strftime 은 로케일 의존).
	 */
	[[nodiscard]] string_t FormatHttpDate(std::chrono::system_clock::time_point _timePoint);

	/**
	 * @brief HTTP-date 문자열을 파싱합니다. 형식이 맞지 않으면 nullopt.
	 *
	 * IMF-fixdate 를 받습니다 — RFC 9110 은 이것만 생성하도록 요구하고, 수신 측은 obsolete 형식
	 * (RFC 850, asctime)도 받아들이도록 권고하지만 실사용에서 사실상 사라졌으므로 지원하지 않습니다.
	 *
	 * @note `Last-Modified`/`If-Modified-Since`/`Expires`/쿠키 만료 처리의 전제입니다.
	 */
	[[nodiscard]] std::optional<std::chrono::system_clock::time_point> ParseHttpDate(string_view_t _text);
}
