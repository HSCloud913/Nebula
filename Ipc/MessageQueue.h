//
// Created by nebula on 24. 5. 29.
//

#pragma once
#include <memory>
#include <span>
#include <vector>
#include "Base/Coroutine/Task.h"
#include "Base/Result.h"
#include "Base/Error.h"
#include "Base/Type.h"

// IEngine 전방 선언 — 헤더 체인을 최소화하기 위해 Engine/IEngine.h 는 .cpp 에서만 include
namespace ne::io
{
	class IEngine;
#if defined(_WIN32)
	class IocpEngine;
#endif
}

BEGIN_NS(ne::ipc)
	class MessageQueue final
	{
	public:
		explicit MessageQueue(string_view_t _name);
		~MessageQueue();

		MessageQueue(MessageQueue&&) noexcept;
		MessageQueue& operator=(MessageQueue&&) noexcept;

		NEBULA_NON_COPYABLE(MessageQueue)

	private:
		class Impl;
		std::unique_ptr<Impl> impl;

	public:
		void_t Connect();
		void_t Listen();

	public:
		// 기존 동기 API — 호환성 유지.
		// 주의(Windows): SendAsync/ReceiveAsync 를 한 번이라도 호출해 핸들이 IocpEngine 에 등록된
		// 뒤에는 Send/Receive 를 더 이상 호출할 수 없다 — 같은 핸들의 모든 완료가 그 IOCP 큐로
		// 몰리므로, 동기 호출의 GetOverlappedResult 대기가 RunOnce() 의 GetQueuedCompletionStatus 와
		// 완료를 두고 경합해 잘못된 타입으로 reinterpret_cast 될 위험이 있다(호출 시 예외로 거부됨).
		void_t Send(std::span<const std::byte> _message) const;
		[[nodiscard]] std::vector<std::byte> Receive() const;

	public:
		// 비동기 API — 둘 다 진짜 Proactor 제출(준비완료 대기 후 별도 syscall 이 아니라
		// I/O 자체를 커널에 제출하고 완료를 기다림).
		// POSIX: AF_UNIX SOCK_SEQPACKET → IEngine::SubmitSend/SubmitReceive
		// Windows: 명명 파이프를 FILE_FLAG_OVERLAPPED 로 열고 IocpEngine 에 등록해
		//          SubmitRead/SubmitWrite 로 완료 기반 비동기 I/O 수행
#if defined(_WIN32)
		[[nodiscard]] ne::Task<ne::Result<void_t, ne::OsError>> SendAsync(std::span<const std::byte> _message, ne::io::IocpEngine& _engine);
		[[nodiscard]] ne::Task<ne::Result<std::vector<std::byte>, ne::OsError>> ReceiveAsync(ne::io::IocpEngine& _engine);
#else
		[[nodiscard]] ne::Task<ne::Result<void_t, ne::OsError>> SendAsync(std::span<const std::byte> _message, ne::io::IEngine& _engine);
		[[nodiscard]] ne::Task<ne::Result<std::vector<std::byte>, ne::OsError>> ReceiveAsync(ne::io::IEngine& _engine);
#endif
	};

END_NS
