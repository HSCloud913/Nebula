//
// Created by hscloud on 26. 6. 30.
//

#pragma once

#if defined(_WIN32)
#	include <memory>
#	include <mutex>
#	include <unordered_map>
#	include <unordered_set>
#	include <vector>
#	include "Base/Handle.h"
#	include "Io/Engine/IEngine.h"
#	include "Io/Engine/Iocp/Provider/RioProvider.h"

BEGIN_NS(ne::io)
	/**
	 * @class IocpEngine
	 * @brief Windows IOCP(I/O Completion Port) 기반 proactor 엔진.
	 *
	 * WSARecv/WSASend/ReadFile/WriteFile/AcceptEx/ConnectEx/TransmitFile 등을 OVERLAPPED 요청으로
	 * 제출하고, GetQueuedCompletionStatusEx 로 완료를 batch 수확해 IEngine::Completion 으로
	 * 정규화한다. 제출된 op 마다 IocpOperation 을 heap 에 두고 그 첫 멤버인 OVERLAPPED* 를 커널에
	 * 넘겨 완료 시 reinterpret_cast 로 복원한다. RioProvider 를 합성해 RIO(Registered I/O) 기반
	 * SendZeroCopy 경로를 AsRegisteredBufferProvider() 로 노출하며, RIO 완료는 별도 RIO_CQ 를
	 * 같은 IOCP 핸들에 바인딩(RioKey)해 일반 op 완료와 구분한다.
	 *
	 * @note Cancel() 은 RIO 로 제출된 op 는 추적하지 않는다.
	 */
	class IocpEngine final :public IEngine
	{
	public:
		/**
		 * @brief CreateIoCompletionPort 로 새 IOCP 핸들을 만들고, 성공하면 RioProvider 를 함께 구성한다.
		 * @param _concurrentThreads 동시에 완료를 처리할 수 있는 스레드 수 힌트(0이면 시스템 CPU 개수만큼 허용).
		 * @note 생성 실패 시 iocpHandle 이 비어 있게 되며 IsValid() 가 false 를 반환한다.
		 */
		explicit IocpEngine(ulong_t _concurrentThreads = 0) noexcept;
		virtual ~IocpEngine() override = default;

		NEBULA_NON_COPYABLE_MOVABLE(IocpEngine)

	private:
		static constexpr ulonglong_t WakeKey = 1;
		static constexpr ulonglong_t RioKey = 2;
		static constexpr int_t MaxBatch = 128;
		static constexpr std::size_t AcceptAddressBufferSize = 256;

		struct IocpOperation
		{
			OVERLAPPED overlapped{};
			void_t* userData{ nullptr };
			HANDLE handle{ nullptr };
			RequestKind requestKind{ RequestKind::READ };
			socket_t acceptSocket{ InvalidSocket };
			socket_t contextSocket{ InvalidSocket };
			longlong_t syncResult{ 0 };
			bool_t hasSyncResult{ false };
			char acceptBuffer[AcceptAddressBufferSize]{};
			std::vector<WSABUF> wsaBuffers;
		};

		using IocpHandle = ne::Handle<HANDLE, decltype([](const HANDLE _handle) { ::CloseHandle(_handle); })>;

	private:
		IocpHandle iocpHandle;
		std::mutex mutex;
		std::unordered_set<ulonglong_t> associated;
		std::unordered_map<void_t*, IocpOperation*> inflight;
		void_t* acceptExPtr{ nullptr };
		void_t* connectExPtr{ nullptr };
		bool_t isExtensionsLoaded{ false };
		std::unique_ptr<RioProvider> rioProvider;

	public: /* IEngine */
		/**
		 * @brief 요청을 OVERLAPPED 기반 Win32/Winsock 비동기 API 호출로 변환해 제출한다.
		 *
		 * SEND_ZERO_COPY 는 RioProvider::SubmitSendRegistered 로 위임하고(RIO 는 IOCP 와 별개의
		 * RIO_CQ 를 사용하므로 IocpOperation 을 만들지 않는다), 그 외 모든 op 는 IocpOperation 을
		 * heap 에 할당해 대상 HANDLE 을 IOCP 에 연관(EnsureAssociated)시킨 뒤 Dispatch() 로
		 * 실제 시스템 콜을 실행한다. 동기적으로 이미 실패가 확정된 경우(연관 실패 등)에는
		 * PostQueuedCompletionStatus 로 가짜 완료를 큐에 넣어 WaitCompletions() 가 일관된 경로로
		 * 결과를 처리하게 한다.
		 */
		virtual void_t Submit(const Request& _request) override;

		/**
		 * @brief GetQueuedCompletionStatusEx 로 완료를 배치 수확해 IEngine::Completion 으로 정규화한다.
		 *
		 * 한 번의 대기로 최대 min(_max, MaxBatch) 개의 OVERLAPPED_ENTRY 를 받아온다. 완료 키가
		 * RioKey 이면 일반 op 가 아니라 RIO 완료 도착 신호이므로 DrainRioCompletions() 로 위임한다.
		 * 그 외 항목은 lpOverlapped 를 IocpOperation* 로 복원해 inflight 맵에서 제거하고, Internal
		 * 필드(NTSTATUS)가 음수면 RtlNtStatusToDosError 로 Win32 에러 코드로 변환한다. ACCEPT 완료는
		 * SO_UPDATE_ACCEPT_CONTEXT 로 accept 소켓에 리스닝 소켓 속성을 상속시켜야 getpeername 등이
		 * 정상 동작하고, CONNECT 완료는 SO_UPDATE_CONNECT_CONTEXT 로 소켓 옵션 상속을 완료해야 한다.
		 *
		 * @return 채워진 완료 개수. GetQueuedCompletionStatusEx 자체가 실패/타임아웃하면 0.
		 */
		[[nodiscard]] virtual int_t WaitCompletions(Completion* _out, int_t _max, std::chrono::milliseconds _timeout) override;

		/** @brief WakeKey 를 완료 키로 하는 더미 완료를 IOCP 에 게시해 대기 중인 스레드를 깨운다. */
		virtual void_t Wake() override { ::PostQueuedCompletionStatus(iocpHandle.Get(), 0, WakeKey, nullptr); }

		/**
		 * @brief 진행 중인 요청의 OVERLAPPED I/O 취소를 시도한다.
		 *
		 * inflight 맵에서 userData 로 IocpOperation 을 찾아 CancelIoEx 를 호출한다. CancelIoEx 는
		 * 비동기적으로 동작하므로 이 호출이 반환된 시점에 취소가 완료되었다는 보장은 없으며,
		 * 실제 취소 여부는 이후 WaitCompletions() 의 완료 결과(취소 성공 시 보통 ERROR_OPERATION_ABORTED)로
		 * 확인해야 한다.
		 *
		 * @note RIO 로 제출된 요청(SEND_ZERO_COPY)은 inflight 에 등록되지 않으므로 이 함수로 취소할 수 없다.
		 */
		virtual void_t Cancel(void_t* _userData) noexcept override;

		/** @brief SEND_FILE_ZERO_COPY/SEND_MEM_ZERO_COPY 지원(TransmitFile/RIO), 나머지 미지원임을 알린다. */
		[[nodiscard]] virtual bool_t Supports(Capability _capability) const noexcept override;
		/** @brief CreateIoCompletionPort 로 유효한 IOCP 핸들을 확보했는지 반환한다. */
		[[nodiscard]] virtual bool_t IsValid() const noexcept override { return static_cast<bool_t>(iocpHandle); }
		/** @brief RIO 기반 zero-copy 등록 버퍼 provider(RioProvider)를 노출한다. */
		[[nodiscard]] virtual IRegisteredBufferProvider* AsRegisteredBufferProvider() noexcept override { return rioProvider.get(); }

	private:
		/**
		 * @brief 대상 HANDLE 을 이 IOCP 에 아직 연관시키지 않았다면 CreateIoCompletionPort 로 연관시킨다.
		 *
		 * 같은 핸들을 중복 연관하면 실패하므로 associated 셋으로 이미 연관된 핸들을 추적해 한 번만
		 * 시도한다. 소켓/파일 핸들은 열릴 때마다 값이 재사용될 수 있으므로, 이 캐시는 엔진 생명
		 * 주기 동안 값이 우연히 재사용되는 경우를 구분하지 않는다는 점에 유의해야 한다.
		 *
		 * @param _handle 연관시킬 파일/소켓 핸들.
		 * @return 이미 연관되어 있었거나 새로 연관에 성공하면 true, CreateIoCompletionPort 실패 시 false.
		 */
		[[nodiscard]] bool_t EnsureAssociated(HANDLE _handle) noexcept;

		/**
		 * @brief AcceptEx/ConnectEx 함수 포인터를 WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)로 최초 1회 로드한다.
		 *
		 * 두 함수 모두 표준 Winsock API 가 아니라 소켓별로 WSAIoctl 을 통해 얻어야 하는 확장
		 * 함수이므로, 최초 사용 시점에 한 번만 조회해 acceptExPtr/connectExPtr 에 캐시해 둔다.
		 * mutex 로 보호되어 여러 스레드에서 동시에 호출되어도 한 번만 로드된다.
		 *
		 * @param _socket 확장 함수 포인터를 조회할 소켓(어떤 소켓이든 같은 프로토콜이면 함수 포인터는 공유 가능).
		 * @return 두 포인터 모두 로드에 성공하면 true.
		 */
		[[nodiscard]] bool_t EnsureExtensions(socket_t _socket) noexcept;

		/**
		 * @brief 준비된 IocpOperation 을 실제 Win32/Winsock 비동기 호출로 실행한다.
		 *
		 * _request.chain 이 있고 op 가 READ/WRITE 인 파일 I/O 는 세그먼트별 OVERLAPPED 를 순차
		 * 실행하며 완료 이벤트로 직접 기다린 뒤(GetOverlappedResult) 합산 결과를 동기 완료로
		 * 게시한다(비동기 API 가 단일 버퍼 체인을 지원하지 않기 때문에 이 경로에서만 예외적으로
		 * 동기 처리한다). 그 외에는 op 별로 ReadFile/WriteFile/WSARecv/WSASend/AcceptEx/ConnectEx/
		 * TransmitFile 을 호출하고, ERROR_IO_PENDING/WSA_IO_PENDING 이 아닌 즉시 실패만
		 * syncError 로 기록해 PostQueuedCompletionStatus 로 동기 완료를 게시한다(IO_PENDING 은
		 * 정상적으로 커널에 넘어간 것이므로 나중에 IOCP 로 통지된다).
		 *
		 * @param _operation 이 호출 동안 채워지는 OVERLAPPED/보조 필드를 담은 heap 객체.
		 * @param _request 원본 요청(버퍼/주소/체인 등).
		 * @param _handle 대상 파일/소켓 핸들.
		 */
		void_t Dispatch(IocpOperation* _operation, const Request& _request, HANDLE _handle) noexcept;

		/**
		 * @brief RIO 완료 큐(RIO_CQ)에서 완료된 RIO 요청들을 뽑아 Completion 배열로 채운다.
		 *
		 * IOCP 완료 키가 RioKey 로 온 시점에 호출되며, RIODequeueCompletion 으로 한 번에 여러
		 * 결과를 배치 회수한다. RIO_CQ 는 통지가 한 번 오면 재수신 등록(ArmNotify)을 다시 해야
		 * 다음 완료가 또 IOCP 로 전달되므로, 회수 후 항상 ArmNotify() 를 호출해 재무장한다.
		 * @return 채워진 완료 개수. rioProvider 가 없거나 미초기화 상태면 0.
		 */
		[[nodiscard]] int_t DrainRioCompletions(Completion* _out, int_t _max) noexcept;

	public:
		/** @brief 내부 IOCP 핸들을 반환한다(다른 컴포넌트가 같은 포트에 핸들을 추가로 연관시킬 때 사용). */
		[[nodiscard]] HANDLE NativeHandle() const noexcept { return iocpHandle.Get(); }
	};

END_NS

#endif // _WIN32
