//
// Created by hscloud on 26. 7. 1.
//

#pragma once
#include <chrono>
#include "Base/Type.h"
#include "Io/IoType.h"
#include "Io/Context/Operation.h"

BEGIN_NS(ne::io)
	enum class EngineType : uint_t
	{
		REACTOR,
		PROACTOR
	};

	/**
	 * @class IEngine
	 * @brief 비동기 I/O 엔진의 공통 인터페이스.
	 *
	 * Submit/WaitCompletions/Wake/Cancel/Supports 로 이루어진 완료 기반(proactor) 계약을 정의한다.
	 * IOCP(Windows)/io_uring(Linux) 처럼 원래 완료 기반인 백엔드는 이를 그대로 매핑하고,
	 * WSAPoll/epoll 처럼 readiness 기반(reactor)인 백엔드는 내부에서 read/write 를 직접 수행해
	 * synthetic completion 을 만들어 동일한 시그니처로 노출한다. 어떤 엔진이 실제로 도는지는
	 * Supports(Capability) 로만 런타임 질의할 수 있다.
	 */
	class IEngine
	{
	public:
		NEBULA_NON_COPYABLE_MOVABLE(IEngine)

		IEngine() = default;
		virtual ~IEngine() = default;

	public:
		/**
		 * @brief 비동기 I/O 요청 하나를 엔진에 제출한다.
		 *
		 * 완료 기반(IOCP/io_uring) 엔진은 커널/드라이버에 요청을 넘기고 즉시 반환하며, 결과는
		 * 이후 WaitCompletions() 를 통해서만 받을 수 있다. readiness 기반(epoll/WSAPoll) 엔진은
		 * 우선 non-blocking 으로 즉시 수행을 시도하고, 당장 처리 불가능하면(EAGAIN 등) 내부
		 * pending 목록에 넣어두었다가 대상 fd 가 준비되는 시점에 WaitCompletions() 안에서 재시도한다.
		 * 어느 경우든 이 함수 자체는 결과를 반환하지 않고, 완료는 항상 _request.userData 를 키로
		 * 한 Completion 으로 나중에 통지된다.
		 * @param _request 대상 handle/RequestKind/버퍼/오프셋 및 완료 시 식별에 쓸 userData 를 담은 요청.
		 */
		virtual void_t Submit(const Request& _request) = 0;

		/**
		 * @brief 완료된 I/O 결과를 최대 _max 개까지 받아온다(배치 수확).
		 *
		 * IOCP 는 GetQueuedCompletionStatusEx, io_uring 은 peek_batch_cqe/wait_cqe_timeout,
		 * epoll/WSAPoll 은 readiness 대기 후 직접 수행한 결과를 모아 동일한 Completion 배열로
		 * 채운다. 한 번의 시스템 콜/대기로 여러 완료를 한꺼번에 회수해 콜당 오버헤드를 줄이는
		 * 것이 목적이며, 이미 쌓여 있는 완료(ready 큐 등)가 있으면 대기 없이 즉시 반환한다.
		 * @param _out 결과를 채울 버퍼(최소 _max 개 원소 보장 필요).
		 * @param _max _out 이 수용 가능한 최대 개수.
		 * @param _timeout 대기 시간(음수면 무한 대기, 0이면 즉시 폴링).
		 * @return 실제로 채워진 완료 개수. 타임아웃이거나 오류면 0을 반환할 수 있다.
		 * @note 스레드 안전성은 구현체별로 다르며, 보통 단일 루프 스레드에서 호출한다고 가정한다.
		 */
		[[nodiscard]] virtual int_t WaitCompletions(Completion* _out, int_t _max, std::chrono::milliseconds _timeout) = 0;

		/**
		 * @brief WaitCompletions() 로 블로킹 중인 루프 스레드를 강제로 깨운다.
		 *
		 * 다른 스레드에서 Submit/Cancel 을 호출했거나 종료를 요청했을 때, 대기 중인
		 * WaitCompletions() 가 즉시 반환하도록 하기 위해 쓰인다. 내부적으로 eventfd(POSIX)나
		 * 루프백 소켓 쌍(Windows) 등 플랫폼 고유의 깨우기 메커니즘에 신호를 보낸다.
		 * @note 완료 대기 중이 아닐 때 호출해도 안전하며, 다음 WaitCompletions() 호출 시 즉시 소비된다.
		 */
		virtual void_t Wake() = 0;

		/**
		 * @brief 아직 완료되지 않은 요청의 취소를 시도한다.
		 *
		 * 취소가 즉시 반영되지 않을 수 있다(이미 커널에 넘어간 요청은 다음 배치까지 취소가
		 * 지연될 수 있음). 성공적으로 취소되면 해당 userData 에 대해 취소를 나타내는 에러 코드
		 * (예: ECANCELED/ERROR_OPERATION_ABORTED)를 담은 Completion 이 이후 WaitCompletions() 에서
		 * 나온다. 이미 완료되었거나 존재하지 않는 userData 는 조용히 무시된다.
		 * @param _userData Submit() 에 전달했던 것과 동일한 식별자.
		 */
		virtual void_t Cancel(void_t* _userData) noexcept = 0;

		/**
		 * @brief 현재 엔진이 특정 기능(zero-copy 송신 등)을 지원하는지 질의한다.
		 * @param _capability 확인할 능력 종류.
		 * @return 지원하면 true. 엔진/플랫폼 조합에 따라 정적으로 결정되는 값이다.
		 */
		[[nodiscard]] virtual bool_t Supports(Capability _capability) const = 0;

		/**
		 * @brief 엔진이 정상적으로 초기화되어 사용 가능한 상태인지 확인한다.
		 * @return 생성자에서 필요한 커널 리소스(IOCP 핸들, epoll fd, io_uring 등)를 모두 확보했으면 true.
		 */
		[[nodiscard]] virtual bool_t IsValid() const noexcept = 0;

		/**
		 * @brief 이 엔진이 등록 버퍼(zero-copy) provider 를 겸하는지 확인하고 그 인터페이스를 얻는다.
		 * @return RIO/io_uring Fixed Buffer 처럼 사전 등록 버퍼를 지원하는 엔진은 해당 provider
		 * 인스턴스를, 그렇지 않으면 nullptr 을 반환한다(기본 구현).
		 */
		[[nodiscard]] virtual class IRegisteredBufferProvider* AsRegisteredBufferProvider() noexcept { return nullptr; }
	};

END_NS
