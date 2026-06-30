//
// Created by hscloud on 25. 6. 29.
//

#if defined(_WIN32)
#include "IocpEngine.h"
#include <winsock2.h>

BEGIN_NS(ne::network)
	IocpEngine::IocpEngine(const ulong_t _concurrentThreads)
		: iocpHandle(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, _concurrentThreads)) {}



	ne::Result<void, ne::OsError> IocpEngine::Watch(const socket_t _fd, const uint32_t _events, IoCallback _callback)
	{
		// 이미 등록된 fd 는 컨텍스트만 갱신 (IOCP 는 재등록 불필요).
		if (const auto it = contexts.find(_fd); it != contexts.end())
		{
			it->second->events = _events;
			it->second->callback = std::move(_callback);
			return ne::Result<void, ne::OsError>::Ok();
		}

		// 새 소켓을 IOCP 에 연결.
		auto* ctx = new IocpContext{};
		ctx->fd = _fd;
		ctx->events = _events;
		ctx->callback = std::move(_callback);

		const HANDLE assoc = ::CreateIoCompletionPort(
			reinterpret_cast<HANDLE>(_fd),
			iocpHandle.Get(),
			reinterpret_cast<ULONG_PTR>(ctx),  // completion key = 컨텍스트 포인터
			0
		);

		if (assoc == nullptr)
		{
			delete ctx;
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[IocpEngine/Watch]")
			);
		}

		contexts[_fd] = ctx;

		// Read 이벤트 요청 시 zero-byte WSARecv 로 "소켓 준비" 알림 등록.
		// 실제 데이터 수신은 콜백을 받은 Stream 레이어가 담당.
		if (_events & IoEvent::Read)
		{
			WSABUF wsaBuf{ .len = 0, .buf = nullptr };
			DWORD flags = 0;
			::WSARecv(
				static_cast<SOCKET>(_fd),
				&wsaBuf, 1,
				nullptr, &flags,
				&ctx->overlapped,
				nullptr
			);
			// WSARecv 가 즉시 완료되지 않아도 GQCS 에서 처리됨.
			// ERROR_IO_PENDING 은 정상 경로.
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::Unwatch(socket_t _fd)
	{
		ReleaseContext(_fd);
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::RunOnce(int_t _timeoutMs)
	{
		DWORD effectiveTimeout = (_timeoutMs < 0) ? INFINITE : static_cast<DWORD>(_timeoutMs);
		if (timerWheel)
		{
			const int_t nextExpiry = timerWheel->NextExpiryMs();
			if (nextExpiry >= 0)
			{
				const DWORD timerTimeout = static_cast<DWORD>(nextExpiry);
				if (effectiveTimeout == INFINITE || timerTimeout < effectiveTimeout)
					effectiveTimeout = timerTimeout;
			}
		}

		DWORD bytesTransferred = 0;
		ULONG_PTR completionKey = 0;
		OVERLAPPED* overlapped = nullptr;

		const BOOL ok = ::GetQueuedCompletionStatus(
			iocpHandle.Get(),
			&bytesTransferred,
			&completionKey,
			&overlapped,
			effectiveTimeout
		);

		if (overlapped == nullptr)
		{
			// timeout 또는 PostQueuedCompletionStatus(nullptr) 종료 신호
			if (!ok && ::GetLastError() == WAIT_TIMEOUT)
			{
				if (timerWheel) timerWheel->Tick();
				return ne::Result<void, ne::OsError>::Ok();
			}

			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[IocpEngine/RunOnce]")
			);
		}

		auto* ctx = reinterpret_cast<IocpContext*>(completionKey);

		if (!ok)
		{
			// 소켓 에러 or 연결 종료
			ctx->callback(ctx->fd, IoEvent::Error | IoEvent::HangUp);
			ReleaseContext(ctx->fd);
			if (timerWheel) timerWheel->Tick();
			return ne::Result<void, ne::OsError>::Ok();
		}

		// 완료된 이벤트 유형 판별 (overlapped 시작 주소로 Read/Write 구별)
		// 현재는 WSARecv 완료 = Read 이벤트로 단순화.
		uint32_t evts = IoEvent::Read;
		if (bytesTransferred == 0) evts |= IoEvent::HangUp;  // 0 바이트 = 상대방 연결 종료

		ctx->callback(ctx->fd, evts);
		if (timerWheel) timerWheel->Tick();
		return ne::Result<void, ne::OsError>::Ok();
	}



	void IocpEngine::ReleaseContext(const socket_t _fd)
	{
		if (const auto context = contexts.find(_fd); context != contexts.end())
		{
			delete context->second;
			contexts.erase(context);
		}
	}

END_NS

#endif // _WIN32
