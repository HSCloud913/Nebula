//
// Created by csw on 26. 7. 7..
//

#pragma once
#include <coroutine>
#include "Type.h"
#include "Buffer/BufferBlock.h"

BEGIN_NS(ne::io)
	// 통합 비동기 완료 컨텍스트. 소켓 proactor + 파일 proactor 모두 사용.
	// 엔진은 완료 시 result 를 채운 뒤 handle.resume() 을 호출한다.
	// Windows: OVERLAPPED 가 반드시 첫 멤버여야 GQCS reinterpret_cast 가 성립한다.
	//
	// 수명: IoContextHolder(Awaitable.h) 가 heap 에 두고 소유권을 엔진과 교대한다 —
	// 코루틴 프레임이 완료 전 파괴돼도 안전하다(단일 스레드 이벤트 루프 전제).
	//   completed : 엔진이 완료를 회수하며(재개 전에) true 로 세팅.
	//   abandoned : 완료 전 프레임이 파괴돼 holder 가 소유권을 넘김 → 엔진이 resume 없이 delete.
	struct ProactorContext
	{
#if defined(_WIN32)
		OVERLAPPED overlapped{};
#endif
		std::coroutine_handle<> handle;
		ne::Result<std::size_t, ne::OsError> result{ ne::Result<std::size_t, ne::OsError>::Ok(0) };
		bool_t isCompleted{ false };
		bool_t isAbandoned{ false };
		// Proactor op 진행 중 데이터 버퍼(BufferBlock)를 살려두는 소유 ref — 커널이 비동기로
		// 읽/쓰는 버퍼가 완료(또는 abandon) 전에 풀로 반납되는 것을 막는다. IoContext 수명과
		// 버퍼 수명을 묶는다(= 1b 로 견고해진 IoContext 수명에 편승). owner 없는(raw) 버퍼면 nullptr.
		BufferBlock* bufferOwner{ nullptr };

		~ProactorContext() noexcept { if (bufferOwner) bufferOwner->Release(); }
	};

END_NS
