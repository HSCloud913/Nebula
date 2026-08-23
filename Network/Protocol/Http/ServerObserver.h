//
// Created by hscloud on 26. 8. 12.
//

#pragma once
#include <chrono>
#include <functional>
#include "Base/Type.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"
#include "Network/Protocol/Http/Message/Method.h"
#include "Network/Protocol/Http/Message/Version.h"

namespace ne::network::http
{
	/**
	 * @class AccessRecord
	 * @brief 처리 완료된 요청 하나의 관측 정보입니다(액세스 로그 한 줄에 해당).
	 *
	 * @note peerAddress 는 아직 채워지지 않습니다 — io::Socket 에 주소 접근자가 없어서이며, 그것이
	 * 추가되면 여기로 이어집니다. 필드를 미리 두는 이유는 나중에 구조를 바꾸지 않기 위해서입니다.
	 */
	struct AccessRecord
	{
		Method method{ Method::GET };
		string_t target;
		int_t statusCode{ 0 };
		Version version{ Version::HTTP_1_1 };

		std::size_t requestBodyBytes{ 0 };
		std::size_t responseBodyBytes{ 0 };

		// 요청을 다 읽은 시점부터 응답 전송을 마친 시점까지.
		std::chrono::milliseconds duration{ 0 };

		string_t peerAddress; // 아직 비어 있음(위 @note 참고)
	};

	/**
	 * @class ServerObserver
	 * @brief 서버 내부에서 일어난 일을 밖으로 내보내는 콜백 묶음입니다 — 액세스 로그·에러 추적용.
	 *
	 * 서버는 연결 하나의 실패가 다른 연결에 영향을 주지 않도록 대부분의 에러를 삼킵니다. 그 자체는
	 * 옳지만, 그러면 운영자가 "왜 그 연결이 죽었는지" 를 볼 방법이 사라집니다. 이 훅은 삼켜지는
	 * 정보만 밖으로 흘려보내며, 설정하지 않으면 아무 비용도 들지 않습니다(빈 std::function 검사 1회).
	 *
	 * @note 콜백은 **이벤트 루프 스레드에서 동기 호출**됩니다. 파일 I/O 나 네트워크처럼 블로킹하는
	 * 작업을 직접 하면 그 시간만큼 서버 전체가 멈춥니다 — 비동기 로거(ne::Logger)의 큐에 넣거나
	 * 값을 복사해 다른 스레드로 넘기세요.
	 * @note ResponseCallbacks 와 달리 반환값이 없습니다. 관측은 흐름을 바꾸지 않습니다.
	 */
	struct ServerObserver
	{
		/** @brief 요청/응답 한 쌍이 완결될 때마다 호출됩니다(액세스 로그). */
		std::function<void_t(const AccessRecord& _record)> onAccess;

		/**
		 * @brief 연결 처리 중 발생한, 사용자에게 응답으로 전달되지 않는 에러마다 호출됩니다.
		 * @param _error 원인. @param _phase 발생 단계("Accept"/"Tls"/"Read"/"Dispatch"/"Write"/"Frame").
		 */
		std::function<void_t(const HttpError& _error, string_view_t _phase)> onError;

		/** @brief 연결이 수립될 때(true)와 닫힐 때(false) 호출됩니다 — 동시 연결 수 계측용. */
		std::function<void_t(bool_t _isOpened)> onConnection;
	};
}
