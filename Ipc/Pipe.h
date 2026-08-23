//
// Created by nebula on 24. 5. 29.
//

#pragma once
#include <memory>
#include <span>
#include <stop_token>

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

class Pipe final
{
public:
	explicit Pipe(string_view_t _name);
	~Pipe();

	Pipe(Pipe&&) noexcept;
	Pipe& operator=(Pipe&&) noexcept;

	NEBULA_NON_COPYABLE(Pipe)

private:
	class Impl;
	std::unique_ptr<Impl> impl;

public:
	void_t Connect();
	void_t Listen();

public:
	// 기존 동기 API — 호환성 유지.
	// 주의(Windows): ReadAsync/WriteAsync 를 한 번이라도 호출해 핸들이 IocpEngine 에 등록된
	// 뒤에는 Read/Write 를 더 이상 호출할 수 없다(MessageQueue 와 동일한 이유 — 완료가 IOCP
	// 큐로 몰려 동기 대기와 RunOnce() 가 경합할 수 있다).
	[[nodiscard]] longlong_t Read(std::span<std::byte> _buffer) const;
	[[nodiscard]] bool_t Write(std::span<const std::byte> _data) const;

public:
	// 비동기 API — 둘 다 진짜 Proactor 제출(준비완료를 기다린 뒤 별도 syscall 을 부르는 것이 아니라
	// I/O 자체를 커널에 제출하고 완료를 기다린다).
	//   Windows: 명명 파이프를 FILE_FLAG_OVERLAPPED 로 열고 READ/WRITE 제출(IOCP 연관은 엔진이 처리)
	//   POSIX:   AF_UNIX SOCK_STREAM 소켓에 RECEIVE/SEND 제출(io_uring 은 IORING_OP_*, epoll 은 에뮬레이션)
	//
	// @note 엔진이 아니라 Context 를 받는다 — 라이브러리의 모든 비동기 표면(Io::File/Socket,
	// Network 전반)이 Context 단위이고, 취소·타이머·완료 회수가 모두 그 단위로 묶여 있다.
	// 엔진을 직접 받으면 호출자가 Context 없이 op 을 제출할 수 있게 되는데, 그러면 완료를
	// 회수해 줄 주체가 없다.
	// @note _stopToken 이 취소되면 커널에 취소를 요청한다. 토큰을 넘기지 않으면 이 op 은
	// 완료될 때까지 취소할 수 없다 — 상대가 영원히 쓰지 않는 파이프에서 그대로 멈춘다.
	// @note Read() 와 달리 ReadAsync() 는 연결 종료를 -1 센티널이 아니라 0 바이트로 나타낸다
	// (size_t 는 부호가 없어 -1 을 표현할 수 없다 — Io/File, MessageQueue 와 동일한 관례).
	[[nodiscard]] ne::Task<ne::io::IoResult<std::size_t>> ReadAsync(std::span<std::byte> _buffer, ne::io::Context& _context, std::stop_token _stopToken = {});
	[[nodiscard]] ne::Task<ne::io::IoResult<std::size_t>> WriteAsync(std::span<const std::byte> _data, ne::io::Context& _context, std::stop_token _stopToken = {});

public:
	[[nodiscard]] bool_t IsConnected() const noexcept;
};

}
