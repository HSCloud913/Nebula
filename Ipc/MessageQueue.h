//
// Created by nebula on 24. 5. 29.
//

#pragma once
#include <memory>
#include <span>
#include <stop_token>
#include <vector>
#include "Base/Coroutine/Task.h"
#include "Base/Result.h"
#include "Base/Error.h"
#include "Base/Type.h"
#include "Io/Diagnostic/Error.h"

// Context 는 참조로만 쓰므로 전방 선언으로 충분하다 — Io/Context.h 는 .cpp 에서만 include.
namespace ne::io
{
	class Context;
}

namespace ne::ipc
{

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
	// 비동기 API — 둘 다 진짜 Proactor 제출(준비완료 대기 후 별도 syscall 이 아니라 I/O 자체를
	// 커널에 제출하고 완료를 기다림).
	//   Windows: 명명 파이프를 FILE_FLAG_OVERLAPPED 로 열고 READ/WRITE 제출(IOCP 연관은 엔진이 처리)
	//   POSIX:   AF_UNIX SOCK_SEQPACKET 소켓에 RECEIVE/SEND 제출
	//
	// @note 엔진이 아니라 Context 를 받는 근거와 _stopToken 의 의미는 Pipe 의 같은 API 설명 참고.
	// @note ReceiveAsync 는 메시지 경계를 보존한다(Windows: PIPE_READMODE_MESSAGE,
	// POSIX: SOCK_SEQPACKET). 한 번 호출이 정확히 한 메시지를 돌려주며, 0 바이트는 상대의 종료다.
	[[nodiscard]] ne::Task<ne::io::IoResult<void_t>> SendAsync(std::span<const std::byte> _message, ne::io::Context& _context, std::stop_token _stopToken = {});
	[[nodiscard]] ne::Task<ne::io::IoResult<std::vector<std::byte>>> ReceiveAsync(ne::io::Context& _context, std::stop_token _stopToken = {});
};

}
