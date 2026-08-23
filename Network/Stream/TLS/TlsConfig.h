//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <vector>
#include "Base/Type.h"

namespace ne::network
{
	/**
	 * @class TlsConfig
	 * @brief TLS 연결/수락에 필요한 인증서, 검증 정책, ALPN 설정을 담는 값 타입입니다.
	 */
	struct TlsConfig
	{
		bool_t isPeerVerificationEnabled{ true };
		string_t caFile;      // PEM CA bundle (optional)
		string_t certFile;    // PEM cert (OpenSSL) / PFX path (SChannel server)
		string_t keyFile;     // PEM private key (OpenSSL only)
		string_t pfxPassword; // PFX password (SChannel server only)

		// ALPN 협상 후보(우선순위 순서, 예: {"h2","http/1.1"}). 비어있으면 ALPN 확장 자체를 안 보낸다.
		// 서버(Accept) 쪽에서는 클라이언트 제안 중 이 목록의 우선순위에 맞는 걸 고른다.
		std::vector<string_t> alpnProtocols;
	};
}
