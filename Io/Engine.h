//
// Created by hscloud on 26. 7. 1.
//

#pragma once
#include <chrono>
#include <cstddef>
#include <memory>
#include "Base/Type.h"
#include "Io/Handle.h"
#include "Memory/Buffer/BufferChain.h"

namespace ne::io
{
	/**
	 * @class Capability
	 * @brief 엔진이 제공하는 zero-copy / 오버헤드 감소 세부 기능을 나타내는 열거형.
	 *
	 * 엔진 서브클래스가 아니라 런타임 IEngine::Supports(Capability) 질의로만 확인한다.
	 * 기본 Read/Write 는 모든 엔진이 항상 지원한다.
	 */
	enum class Capability : uint_t
	{
		SEND_FILE_ZERO_COPY,
		SEND_MEM_ZERO_COPY,
		RECEIVE_OVERHEAD_REDUCED,
	};

	/**
	 * @class RequestKind
	 * @brief 엔진에 제출할 수 있는 I/O 연산 종류를 나타내는 열거형.
	 */
	enum class RequestKind : uint_t
	{
		ACCEPT,
		CONNECT,
		READ,
		WRITE,
		WAIT_READABLE,
		WAIT_WRITABLE,
		RECEIVE,
		SEND,
		RECEIVE_FROM,
		SEND_TO,
		SEND_ZERO_COPY,
		SEND_FILE,
	};

	/**
	 * @class Request
	 * @brief 엔진에 제출하는 단일 I/O 요청을 나타내는 값 타입.
	 *
	 * 연산 종류(requestKind)에 따라 사용되는 필드가 달라지는 다목적 구조체이다. 완료 시 userData 를
	 * 그대로 Completion 으로 돌려받으므로, 상위 계층은 여기에 코루틴 재개용 컨텍스트 포인터를
	 * 실어 완료를 자신의 코루틴으로 연결한다.
	 */
	struct Request
	{
		RequestKind requestKind{ RequestKind::READ };
		void_t* userData{ nullptr };
		ulonglong_t handle{ 0 };
		void_t* buffer{ nullptr };
		std::size_t length{ 0 };
		ulonglong_t offset{ 0 };
		ulonglong_t bufferId{ 0 };
		const void_t* address{ nullptr };
		int_t addressLength{ 0 };
		ulonglong_t auxHandle{ 0 };
		const ne::memory::BufferChain* chain{ nullptr };
		void_t* fromAddress{ nullptr };
		int_t* fromAddressLength{ nullptr };
		bool_t isRegisteredIo{ false };
	};

	/**
	 * @class Completion
	 * @brief 엔진이 돌려주는 단일 I/O 완료 결과를 나타내는 값 타입.
	 *
	 * result 가 0 이상이면 성공(전송된 바이트 수), 음수이면 실패(-(OS 에러코드))를 뜻하며
	 * 상위 계층에서 IoError 로 변환한다.
	 */
	struct Completion
	{
		void_t* userData{ nullptr };
		longlong_t result{ 0 };
	};

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
		 *
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
		 *
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
		 *
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
		 *
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

		/**
		 * @brief 핸들이 닫혔음을 엔진에 알려, 그 핸들에 대해 캐시한 내부 상태(예: IOCP 연관 여부)를 정리한다.
		 *
		 * OS 는 닫힌 소켓/파일 핸들 값을 재사용하므로, 닫을 때 알리지 않으면 다음에 같은 값의 새 핸들이
		 * 캐시된 "이미 연관됨" 상태에 잘못 걸려 완료 통지를 받지 못할 수 있다(예: 서버 재접속 시 두 번째
		 * 연결의 Receive 가 영영 완료되지 않음). 해당 캐시가 없는 엔진에서는 no-op 이다.
		 *
		 * @param _handleKey 닫히는 네이티브 핸들 값(소켓/파일 핸들을 정수로 캐스팅한 값).
		 */
		virtual void_t Deregister(ulonglong_t _handleKey) noexcept { (void_t)_handleKey; }
	};

	/**
	 * @brief 플랫폼에 맞는 I/O 엔진 구현을 생성해 IEngine 으로 반환합니다.
	 *
	 * PROACTOR 는 완료 기반 백엔드(Windows: IOCP, Linux: io_uring)를, REACTOR 는 readiness
	 * 기반 백엔드(Windows: WSAPoll, Linux: epoll)를 만듭니다. 호출자는 구체 엔진 타입을 알 필요
	 * 없이 EngineType 만으로 최적 백엔드를 얻습니다. 반환된 엔진의 IsValid() 로 초기화 성공을
	 * 확인하세요.
	 *
	 * @return 생성된 엔진. 지원되지 않는 조합이면 nullptr.
	 */
	[[nodiscard]] std::unique_ptr<IEngine> MakeEngine(EngineType _type = EngineType::PROACTOR);
}
