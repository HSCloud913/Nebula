//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include <coroutine>
#include <cstddef>
#include <optional>
#include <stop_token>
#include <utility>
#include "Base/Type.h"
#include "Base/Error.h"
#include "Io/Context/Context.h"
#include "Io/IoResult.h"
#include "Io/Context/Operation.h"

BEGIN_NS(ne::io)
	/**
	 * @class Awaitable
	 * @brief I/O 완료 한 건을 co_await 로 감싸는 awaitable.
	 *
	 * Request 를 Context 의 엔진에 제출하고, 완료될 때까지 코루틴을 suspend 시킨다.
	 * co_await 결과는 IoResult<std::size_t> 이며, 완료 컨텍스트(CompletionHandler)는 heap 에
	 * 할당되어 소유권이 이 객체와 Context 루프 사이를 오간다: 정상 완료 시 루프가 재개하며
	 * await_resume 이 값을 소비한 뒤 소멸자가 해제하고, 완료 전에 이 객체가 먼저 파괴되면
	 * 소유권을 루프로 넘겨 루프가 완료를 회수할 때 해제한다. stop_token 이 취소되면 진행 중인
	 * op 을 커널 취소 요청한다.
	 */
	class Awaitable
	{
	public:
		Awaitable(Context& _context, const Request& _request, std::stop_token _stopToken = {}) noexcept
			: context(_context)
			, request(_request)
			, stopToken(std::move(_stopToken)) {}

		~Awaitable()
		{
			if (handler == nullptr) return;

			if (!handler->isCompleted) handler->isAbandoned = true;
			else delete handler;
		}

		NEBULA_NON_COPYABLE_MOVABLE(Awaitable)

	private:
		/**
		 * @class CancelInvoker
		 * @brief stop_callback 이 발화할 때 엔진에 커널 취소를 요청하는 함수자.
		 *
		 * userData(CompletionHandler 포인터)를 엔진의 Cancel() 에 그대로 전달한다.
		 */
		struct CancelInvoker
		{
			IEngine* engine;
			void_t* userData;

			void_t operator()() const noexcept { engine->Cancel(userData); }
		};

	private:
		Context& context;
		Request request;
		std::stop_token stopToken;
		CompletionHandler* handler{ nullptr };
		std::optional<std::stop_callback<CancelInvoker>> cancelGuard;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		void_t await_suspend(const std::coroutine_handle<> _handle)
		{
			handler = new CompletionHandler{};
			handler->handle = _handle;
			request.userData = handler;
			context.Engine().Submit(request);

			if (stopToken.stop_possible()) cancelGuard.emplace(stopToken, CancelInvoker{ &context.Engine(), handler });
		}

		[[nodiscard]] IoResult<std::size_t> await_resume() const noexcept
		{
			const longlong_t result = handler->result;
			if (result < 0) return IoResult<std::size_t>::Error(IoError{ ne::OsError{ static_cast<ne::ulong_t>(-result) } });

			return IoResult<std::size_t>::Ok(static_cast<std::size_t>(result));
		}
	};

END_NS
