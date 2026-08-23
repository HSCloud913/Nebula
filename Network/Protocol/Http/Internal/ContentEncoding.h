//
// Created by hscloud on 26. 8. 23.
//

#pragma once
#include "Base/Type.h"
#include "Network/Protocol/Http/Compression.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"
#include "Network/Protocol/Http/Message/Request.h"
#include "Network/Protocol/Http/Message/Response.h"

namespace ne::network::http::internal
{
	/**
	 * @brief 우리가 해제할 수 있는 인코딩을 `Accept-Encoding` 으로 광고합니다.
	 *
	 * @return 이 호출이 헤더를 **직접 넣었으면** true. 호출자가 이미 헤더를 지정해 뒀으면 건드리지 않고 false.
	 *
	 * @note 반환값이 곧 "응답을 자동으로 풀어도 되는가" 의 판단 근거입니다. 사용자가 손으로
	 * `Accept-Encoding` 을 넣었다면 압축된 바이트 자체가 목적일 수 있으므로(프록시·저장·재전송)
	 * 마음대로 풀어 버리면 안 됩니다. curl/reqwest 도 같은 규칙을 씁니다.
	 */
	[[nodiscard]] bool_t ApplyAcceptEncoding(Request& _request);

	/**
	 * @brief `Content-Encoding` 이 붙은 응답 본문을 해제하고 헤더를 실제 상태에 맞게 고칩니다.
	 *
	 * 해제에 성공하면 `Content-Encoding` 을 지우고 `Content-Length` 를 해제된 크기로 갱신합니다 —
	 * 남겨 두면 본문은 평문인데 헤더는 압축이라고 말하는 모순 상태가 되어, 이 응답을 그대로 다시
	 * 내보내는 코드가 깨집니다.
	 *
	 * @param _isAutomatic ApplyAcceptEncoding() 이 헤더를 넣었는지. false 면 아무것도 하지 않습니다.
	 * @return 해제 실패(깨진 스트림·상한 초과·미지원 인코딩) 시 에러. 인코딩이 없거나 identity 면 성공(무동작).
	 */
	[[nodiscard]] HttpResult<void_t> DecodeResponseBody(Response& _response, bool_t _isAutomatic);

	/**
	 * @brief 응답 본문을 클라이언트가 받아들이는 인코딩으로 압축합니다(서버 쪽).
	 *
	 * 다음 중 하나라도 해당하면 아무것도 하지 않습니다 — 각각을 건너뛰는 근거는 구현에 적어 두었습니다:
	 * 클라이언트가 지원을 광고하지 않음 / 이미 `Content-Encoding` 이 붙어 있음 / 스트리밍 본문 /
	 * 설정 하한보다 작음 / 허용 목록에 없는 Content-Type / 본문 없는 상태 코드 / 압축해도 줄지 않음.
	 *
	 * @note 실패해도 에러를 돌려주지 않습니다. 압축은 최적화이므로, 실패했을 때의 올바른 동작은
	 * "압축하지 않고 보내기" 입니다 — 요청 전체를 실패시키면 최적화가 장애 원인이 됩니다.
	 */
	void_t CompressResponseBody(const Request& _request, Response& _response, const Compression& _compression);
}
