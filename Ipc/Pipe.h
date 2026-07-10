//
// Created by nebula on 24. 5. 29.
//

#pragma once
#include <memory>
#include <span>

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

BEGIN_NS (ne::ipc)

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
	// 비동기 API — 둘 다 진짜 Proactor 제출.
	// POSIX: AF_UNIX SOCK_STREAM → IEngine::SubmitSend/SubmitReceive
	// Windows: 명명 파이프를 FILE_FLAG_OVERLAPPED 로 열고 IocpEngine 에 등록해
	//          SubmitRead/SubmitWrite 로 완료 기반 비동기 I/O 수행
	// 주의: Read() 와 달리 ReadAsync() 는 연결 종료를 -1 센티널이 아니라 0 바이트로
	// 나타낸다(Result<size_t,...> 는 부호없는 타입이라 -1 을 표현할 수 없다 — AsyncFile/
	// MessageQueue 의 비동기 API와 동일한 관례).
	#if defined(_WIN32)
	[[nodiscard]] ne::Task<ne::Result<std::size_t, ne::OsError>> ReadAsync(std::span<std::byte> _buffer, ne::io::IocpEngine& _engine); [[nodiscard]] ne::Task<ne::Result<std::size_t, ne::OsError>> WriteAsync(std::span<const std::byte> _data, ne::io::IocpEngine& _engine);
	#else
	[[nodiscard]] ne::Task<ne::Result<std::size_t, ne::OsError>> ReadAsync(std::span<std::byte> _buffer, ne::io::IEngine& _engine);
	[[nodiscard]] ne::Task<ne::Result<std::size_t, ne::OsError>> WriteAsync(std::span<const std::byte> _data, ne::io::IEngine& _engine);
	#endif

public:
	[[nodiscard]] bool_t IsConnected() const noexcept;
};

END_NS
