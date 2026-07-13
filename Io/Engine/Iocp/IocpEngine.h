//
// Created by hscloud on 26. 6. 30.
//
// Level 0 — Windows IOCP 백엔드. proactor(완료 기반) 그대로 매핑한다(스펙 2.1).
//   Submit          : op 별 overlapped 요청 발행(WSARecv/WSASend/ReadFile/WriteFile ...)
//   WaitCompletions : GetQueuedCompletionStatusEx 로 완료 batch 수확 → Completion 으로 정규화
//   Wake            : PostQueuedCompletionStatus(널 overlapped) 로 대기 해제
//   Supports        : capability 매트릭스(스펙 2.2)
//
// 제출 op 당 IocpOperation 을 heap 에 두고 OVERLAPPED* 를 커널에 넘긴다 — 완료 시 그 포인터로
// 복원해 userData 를 되돌린다(OVERLAPPED 가 첫 멤버여야 reinterpret_cast 성립).
// 확장성: 이 완료 큐 모델은 추후 IoRing(Windows 11+) 백엔드도 같은 IEngine 계약으로 흡수 가능.

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
	class IocpEngine final :public IEngine
	{
	public:
		explicit IocpEngine(ulong_t _concurrentThreads = 0) noexcept;
		virtual ~IocpEngine() override = default;

		NEBULA_NON_COPYABLE_MOVABLE(IocpEngine)

	private:
		static constexpr ulonglong_t WakeKey = 1;                   // PostQueuedCompletionStatus 웨이크업 식별 키(널 overlapped 와 함께). 실제 op 완료와 구분.
		static constexpr ulonglong_t RioKey = 2;                    // RIO_CQ 를 이 키로 IOCP 에 바인딩해 일반 op 완료(key=0)/wake(key=WakeKey)와 구분한다.
		static constexpr int_t MaxBatch = 128;                      // WaitCompletions 한 번에 수확할 최대 완료 개수 상한(스택 버퍼).
		static constexpr std::size_t AcceptAddressBufferSize = 256; // AcceptEx 주소 출력 버퍼 크기 — 2 × (최대 sockaddr + 16). sockaddr_in6(28)+16=44, ×2=88 < 256.

		struct IocpOperation
		{ // 제출 op 당 컨텍스트. overlapped 는 반드시 첫 멤버.
			OVERLAPPED overlapped{};
			void_t* userData{ nullptr };
			HANDLE handle{ nullptr };                // CancelIoEx 대상
			OpCode op{ OpCode::READ };               // 완료 후처리 분기용(Accept/Connect)
			socket_t acceptSocket{ InvalidSocket };  // Accept: AcceptEx 로 채워질 새 소켓
			socket_t contextSocket{ InvalidSocket }; // Accept: listen 소켓 / Connect: 연결 소켓 (SO_UPDATE_*)
			longlong_t syncResult{ 0 };              // 동기 제출 실패/완료 시 미리 계산한 결과(성공은 바이트수, 실패는 -에러). hasSyncResult 일 때만 유효.
			bool_t hasSyncResult{ false };
			char acceptBuffer[AcceptAddressBufferSize]{}; // AcceptEx local/remote 주소 출력 버퍼
			std::vector<WSABUF> wsaBuffers;               // chain(scatter/gather) 요청일 때만 채움 — WSARecv/WSASend 에 넘길 배열, 완료까지 살아있어야 함
		};

		using IocpHandle = ne::Handle<HANDLE, decltype([](const HANDLE _handle) { ::CloseHandle(_handle); })>;

	private:
		IocpHandle iocpHandle;
		std::mutex mutex;                                     // associated / inflight / 확장함수 보호(멀티스레드 Submit/WaitCompletions/Cancel)
		std::unordered_set<ulonglong_t> associated;           // 이미 IOCP 에 연결된 handle 집합
		std::unordered_map<void_t*, IocpOperation*> inflight; // userData → 진행 중 op (Cancel 조회용)
		void_t* acceptExPtr{ nullptr };                       // LPFN_ACCEPTEX (lazy, WSAIoctl 로 획득)
		void_t* connectExPtr{ nullptr };                      // LPFN_CONNECTEX
		bool_t isExtensionsLoaded{ false };
		std::unique_ptr<RioProvider> rioProvider; // SendZeroCopy(RIOSend) 전용 — lazy 초기화, IsValid() 와 무관하게 항상 생성

	public:
		virtual void_t Submit(const Request& _request) override;
		[[nodiscard]] virtual int_t WaitCompletions(Completion* _out, int_t _max, std::chrono::milliseconds _timeout) override;
		virtual void_t Wake() override;
		virtual void_t Cancel(void_t* _userData) noexcept override;
		[[nodiscard]] virtual bool_t Supports(Capability _capability) const noexcept override;
		[[nodiscard]] virtual bool_t IsValid() const noexcept override { return static_cast<bool_t>(iocpHandle); }
		[[nodiscard]] virtual IRegisteredBufferProvider* AsRegisteredBufferProvider() noexcept override { return rioProvider.get(); }

	private:
		// handle 을 IOCP 에 최초 1회 연결한다(이미 연결됐으면 no-op). mutex 를 이미 쥔 상태로 호출.
		[[nodiscard]] bool_t EnsureAssociated(HANDLE _handle) noexcept;
		// AcceptEx/ConnectEx 함수 포인터를 lazy 로 획득한다(WSAIoctl). 자체 락.
		[[nodiscard]] bool_t EnsureExtensions(socket_t _socket) noexcept;
		// op 를 커널에 발행하고, 동기 실패면 -에러를 담은 완료를 IOCP 로 되돌린다.
		void_t Dispatch(IocpOperation* _operation, const Request& _request, HANDLE _handle) noexcept;
		// RioKey 로 식별된 IOCP 엔트리 하나를 RIODequeueCompletion 으로 풀어 _out 에 채운다(최대 _max 개).
		[[nodiscard]] int_t DrainRioCompletions(Completion* _out, int_t _max) noexcept;

	public:
		[[nodiscard]] HANDLE NativeHandle() const noexcept { return iocpHandle.Get(); }
	};

END_NS

#endif // _WIN32
