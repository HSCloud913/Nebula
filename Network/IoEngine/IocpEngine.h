//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#if defined(_WIN32)

#include <unordered_map>
#include "IIoEngine.h"
#include "NebulaHandle.h"
#include "NetworkType.h"

BEGIN_NS(ne::network)
	// IOCP 완료 키. 소켓을 CreateIoCompletionPort 로 등록할 때 사용.
	// 콜백 포인터를 completion key 로 전달해 GQCS 완료 시 직접 디스패치.
	class IocpEngine final :public IIoEngine
	{
		NEBULA_NON_COPYABLE_MOVABLE(IocpEngine)

	public:
		// concurrentThreads: GQCS 를 동시에 호출할 수 있는 최대 스레드 수.
		// 0 이면 OS가 논리 프로세서 수로 자동 결정.
		explicit IocpEngine(ulong_t _concurrentThreads = 0);
		virtual ~IocpEngine() override = default;

	private:
		// OVERLAPPED 확장: 완료 시 호출할 콜백과 fd 를 포함.
		struct IocpContext
		{
			OVERLAPPED overlapped{};   // 반드시 첫 멤버 (reinterpret_cast 용)
			socket_t fd{};
			uint32_t events{};
			IoCallback callback;
		};

		using IocpHandle = ne::Handle<
			HANDLE,
			decltype([](const HANDLE _handle) { ::CloseHandle(_handle); })
		>;

		IocpHandle iocpHandle;
		std::unordered_map<socket_t, IocpContext*> contexts;  // 소켓당 활성 컨텍스트

	public:
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Watch(socket_t _fd, uint32_t _events, IoCallback _callback) override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Unwatch(socket_t _fd) override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> RunOnce(int_t _timeoutMs = -1) override;

		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(iocpHandle); }
		[[nodiscard]] HANDLE NativeHandle() const noexcept { return iocpHandle.Get(); }

	private:
		void ReleaseContext(socket_t _fd);
	};

END_NS

#endif // _WIN32
