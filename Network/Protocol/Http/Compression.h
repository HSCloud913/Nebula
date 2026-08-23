//
// Created by hscloud on 26. 8. 23.
//

#pragma once
#include <cstddef>
#include <vector>
#include "Base/Type.h"
#include "Compress/Deflate.h"

namespace ne::network::http
{
	/**
	 * @class Compression
	 * @brief 서버 응답 압축 설정입니다 — ServerBuilder::Compress() 로 켭니다(기본: 꺼짐).
	 *
	 * 기본이 꺼짐인 이유는 압축이 **CPU 를 응답 지연과 교환하는 선택**이기 때문입니다. 대역폭이
	 * 병목이면 큰 이득이지만, 이미 CPU 가 포화된 서버에서는 지연만 늘립니다. 켤지 말지는 운영자가
	 * 알고, 라이브러리가 대신 결정할 일이 아닙니다.
	 *
	 * @note 켜면 압축한 응답에 `Vary: Accept-Encoding` 이 붙습니다. 이것이 없으면 중간 캐시가
	 * gzip 응답을 gzip 을 요청하지 않은 클라이언트에게 돌려줄 수 있습니다 — 그 클라이언트에게는
	 * 그냥 깨진 응답입니다.
	 */
	struct Compression
	{
		/**
		 * @brief 압축을 적용할 최소 본문 크기(바이트).
		 *
		 * @note 작은 본문은 압축해도 줄지 않거나 오히려 커집니다(허프만 표 전송 비용). 게다가 한
		 * 패킷에 들어갈 크기라면 줄여도 전송 시간이 달라지지 않으므로 CPU 만 쓰는 셈입니다.
		 */
		std::size_t minimumBytes{ 1024 };

		/** @brief 압축 레벨(0~9). 서버는 요청마다 압축하므로 기본값(6)보다 올리는 것은 대개 손해입니다. */
		int_t level{ ne::compress::DefaultCompressionLevel };

		/**
		 * @brief 압축할 Content-Type 접두사 목록. 비우면 **모든** 타입을 압축합니다.
		 *
		 * @note 허용 목록을 쓰는 이유는 이미 압축된 형식(JPEG/PNG/MP4/zip)을 다시 압축하면 CPU 만
		 * 쓰고 크기는 그대로이기 때문입니다. 접두사 비교라 "text/" 하나로 text/* 전체를 덮습니다.
		 */
		std::vector<string_t> contentTypes{ "text/", "application/json", "application/javascript", "application/xml", "application/x-javascript", "image/svg+xml" };
	};
}
