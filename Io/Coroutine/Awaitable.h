//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include <coroutine>
#include <cstddef>
#include <cstring>
#include <optional>
#include <stop_token>
#include <utility>
#include "Base/Type.h"
#include "Base/Error.h"
#include "Io/Context.h"
#include "Io/Diagnostic/Error.h"
#include "Io/Engine.h"

namespace ne::io
{
	/**
	 * @class Awaitable
	 * @brief I/O 완료 한 건을 co_await 로 감싸는 awaitable.
	 *
	 * Request 를 Context 의 엔진에 제출하고, 완료될 때까지 코루틴을 suspend 시킨다.
	 * co_await 결과는 IoResult<std::size_t> 이며, 완료 컨텍스트(CompletionHandler)는 heap 에
	 * 할당되어 소유권이 이 객체와 Context 루프 사이를 오간다(HandlerState 규약 참고).
	 *
	 * @note 이 객체가 완료 전에 파괴되면(취소/타임아웃으로 상위 Task 를 폐기 — Task.h 가 문서화한
	 * 정상 취소 경로) **커널에 취소를 요청한다.** 소유권만 루프로 넘기고 취소하지 않으면, 커널은
	 * 계속 이 op 을 들고 있으면서 코루틴 프레임에 있던 버퍼/sockaddr 에 쓰기를 시도한다. 그 프레임은
	 * 이미 파괴됐으므로 use-after-free 다.
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

			// stop_callback 을 가장 먼저 파괴한다. 그러지 않으면 아래에서 소유권을 넘긴 뒤에도 콜백이
			// 발화해, 루프가 이미 해제한 userData 로 Cancel() 을 호출할 수 있다(엔진이 그 포인터를
			// pendingCancels 에 저장하는 리액터에서는 새로 할당된 다른 op 을 취소하게 된다).
			cancelGuard.reset();

			// 루프가 이미 완료를 회수했다면 해제 책임은 우리 쪽이다.
			if (handler->state.exchange(HandlerState::ABANDONED, std::memory_order_acq_rel) == HandlerState::COMPLETED)
			{
				delete handler;
				return;
			}

			// 아직 커널이 op 을 들고 있다 — 취소를 요청해야 handler 소유 메모리에 대한 쓰기가 멈춘다.
			// 취소는 (합성) 완료를 만들고, 루프가 그 완료를 회수할 때 ABANDONED 를 보고 해제한다.
			context.Engine().Cancel(handler);
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

		// RECEIVE_FROM 처럼 커널이 sockaddr 을 **써서** 돌려주는 op 에서, 완료 후 결과를 되돌려 줄
		// 호출자 버퍼. 커널은 handler 소유 저장소에 쓰고, 여기로 복사하는 것은 완료가 확정된 뒤다.
		void_t* outAddress{ nullptr };
		int_t* outAddressLength{ nullptr };
		int_t outAddressCapacity{ 0 };

	public:
		[[nodiscard]] bool await_ready() const noexcept { return false; }

		void await_suspend(const std::coroutine_handle<> _handle)
		{
			handler = new CompletionHandler{};
			handler->handle = _handle;

			RelocateAddressScratch();

			request.userData = handler;
			context.Engine().Submit(request);

			if (stopToken.stop_possible()) cancelGuard.emplace(stopToken, CancelInvoker{ &context.Engine(), handler });
		}

		[[nodiscard]] IoResult<std::size_t> await_resume() noexcept
		{
			const longlong_t result = handler->result;
			if (result < 0) return IoResult<std::size_t>::Error(IoError{ ne::OsError{ static_cast<ne::ulong_t>(-result) } });

			// 커널이 채운 sockaddr 을 호출자에게 되돌려 준다(성공한 경우에만 의미가 있다).
			if (outAddress != nullptr)
			{
				const int_t written = handler->addressLength < outAddressCapacity ? handler->addressLength : outAddressCapacity;
				if (written > 0) std::memcpy(outAddress, handler->addressStorage, static_cast<std::size_t>(written));
				if (outAddressLength != nullptr) *outAddressLength = written;
			}

			return IoResult<std::size_t>::Ok(static_cast<std::size_t>(result));
		}

	private:
		/**
		 * @brief 호출자 프레임에 있던 sockaddr 을 handler 소유 저장소로 옮기고 Request 를 그쪽으로 가리킨다.
		 *
		 * 커널은 CONNECT/SEND_TO 에서 이 메모리를 **읽고**, RECEIVE_FROM 에서 **쓴다.** 두 경우 모두
		 * 완료 시점까지 유효해야 하므로 op 과 수명이 같은 handler 가 소유해야 한다.
		 */
		void_t RelocateAddressScratch() noexcept
		{
			if (request.address != nullptr && request.addressLength > 0 && static_cast<std::size_t>(request.addressLength) <= CompletionHandler::AddressStorageSize)
			{
				std::memcpy(handler->addressStorage, request.address, static_cast<std::size_t>(request.addressLength));
				handler->addressLength = request.addressLength;
				request.address = handler->addressStorage;
			}

			if (request.fromAddress != nullptr)
			{
				outAddress = request.fromAddress;
				outAddressLength = request.fromAddressLength;
				outAddressCapacity = request.fromAddressLength != nullptr ? *request.fromAddressLength : 0;
				if (outAddressCapacity > static_cast<int_t>(CompletionHandler::AddressStorageSize)) outAddressCapacity = static_cast<int_t>(CompletionHandler::AddressStorageSize);

				handler->addressLength = outAddressCapacity;
				request.fromAddress = handler->addressStorage;
				request.fromAddressLength = &handler->addressLength;
			}
		}
	};
}
