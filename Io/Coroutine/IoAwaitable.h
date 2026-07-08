//
// Created by hscloud on 26. 7. 8.
//
// Level 2 — 완료 기반 op 한 건을 co_await 로 감싸는 기본 awaitable. co_await → IoResult<size_t>.
//
// 완료 컨텍스트(IoCompletionHandler)를 heap 에 두고 소유권을 IoContext 루프와 교대한다:
//   정상 완료 : 루프가 completed 세팅 후 resume → await_resume 이 소비 → holder 소멸자가 delete.
//   중도 폐기 : 완료 전 코루틴 프레임 파괴 → holder 소멸자가 abandoned 세팅(소유권을 루프에 넘김)
//               → 루프가 완료 회수 시 resume 없이 delete. (mid-flight use-after-free 방지, 스펙 4)

#pragma once
#include <coroutine>
#include <cstddef>
#include <optional>
#include <stop_token>
#include <utility>
#include "Type.h"
#include "Error.h"
#include "IoContext.h"
#include "IoResult.h"
#include "Engine/IoOperation.h"

BEGIN_NS(ne::io)
	class IoAwaitable
	{
	public:
		NEBULA_NON_COPYABLE_MOVABLE(IoAwaitable)

		// _stopToken 이 stop 을 받으면 진행 중 op 를 커널 취소(CancelIoEx)한다 — op 는 aborted 로 완료돼
		// 코루틴이 에러(ERROR_OPERATION_ABORTED)로 재개된다. 기본값(빈 토큰)은 취소 없음.
		IoAwaitable(IoContext& _context, const IoRequest& _request, std::stop_token _stopToken = {}) noexcept
			: context(_context)
			, request(_request)
			, stopToken(std::move(_stopToken)) {}

		~IoAwaitable()
		{
			if (handler == nullptr) return;

			// 아직 완료 전이면 커널/루프가 참조 중 — 소유권을 루프에 넘긴다(완료 시 루프가 delete).
			if (!handler->completed) handler->abandoned = true;
			else delete handler;
		}

	private:
		// stop_callback 이 부를 취소 함수자 — userData(handler) 로 엔진에 커널 취소를 요청한다.
		struct CancelInvoker
		{
			IIoEngine* engine;
			void*      userData;

			void_t operator()() const noexcept { engine->Cancel(userData); }
		};

		IoContext&           context;
		IoRequest            request;
		std::stop_token      stopToken;
		IoCompletionHandler* handler{ static_cast<IoCompletionHandler*>(nullptr) };
		std::optional<std::stop_callback<CancelInvoker>> cancelGuard;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		void_t await_suspend(const std::coroutine_handle<> _handle)
		{
			handler = new IoCompletionHandler{};
			handler->handle = _handle;
			request.userData = handler;
			context.Engine().Submit(request);

			// 제출 후 취소 콜백 등록. 이미 stop 이 요청됐으면 emplace 시 즉시 발화해 방금 제출한 op 를 취소.
			if (stopToken.stop_possible())
				cancelGuard.emplace(stopToken, CancelInvoker{ &context.Engine(), handler });
		}

		[[nodiscard]] IoResult<std::size_t> await_resume() const noexcept
		{
			const longlong_t result = handler->result;
			if (result < 0)
				return IoResult<std::size_t>::Error(IoError{ ne::OsError{ static_cast<ne::ulong_t>(-result) } });

			return IoResult<std::size_t>::Ok(static_cast<std::size_t>(result));
		}
	};

END_NS
