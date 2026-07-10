//
// Created by nebula on 24. 5. 29.
//

#include "Ipc/MessageQueue.h"

#include "Base/Exception.h"
#include "Util/StringFormat.h"
#include "Io/Engine/IEngine.h"
// SendSubmitAwaitable/ReceiveSubmitAwaitable(소켓 Proactor, POSIX) +
// ReadSubmitAwaitable/WriteSubmitAwaitable(파일 Proactor, Windows/IocpEngine) 전부
// Io 모듈 공용 — Ipc 는 더 이상 자체 Awaitable.h 를 두지 않는다.
#include "Io/Coroutine/Awaitable.h"

#if defined(_WIN32)
#	include <winsock2.h>
#elif defined(IS_POSIX)
// AF_UNIX SOCK_SEQPACKET 기반 — POSIX 메시지 큐(mqd_t)는 io_uring 이 아는 opcode가 없어
// (mq_send/mq_receive 전용 syscall) Reactor(Watch + 동기 mq_send/mq_receive)로만 쓸 수 있었다.
// 소켓으로 바꾸면 SOCK_SEQPACKET 이 메시지 경계를 그대로 보존하면서 IEngine::SubmitSend/
// SubmitReceive(IORING_OP_SEND/RECV)로 진짜 Proactor 제출이 가능해진다 — PlainStream 이
// TCP 소켓에 쓰는 것과 동일한 경로. priority/큐 용량 제한(mq_maxmsg 등)은 기존에 아무
// 호출자도 쓰지 않던 기능이라 잃을 게 없다(대신 MaxMessage 고정 버퍼 크기로 대체).
#	include <sys/socket.h>
#	include <sys/un.h>
#	include <unistd.h>
#	include <cerrno>
#	include <cstring>
#endif



BEGIN_NS (ne::ipc)
#if defined(_WIN32)
class MessageQueue::Impl final
{
public:
	explicit Impl(const string_view_t _name)
		: pipeName(LR"(\\.\pipe\)" + StringFormat::UTF8toWCS(string_t(_name).c_str())) {}
	~Impl() { if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle); }

private:
	wstring_t pipeName;
	HANDLE handle = INVALID_HANDLE_VALUE;
	bool_t isRegistered{ false }; // IocpEngine 등록 여부 — 최초 SendAsync/ReceiveAsync 에서 1회만 등록(RegisterFileHandle 은 멱등하지 않음)

public:
	[[nodiscard]] HANDLE Handle() const noexcept { return handle; }
	static constexpr ulong_t MaxMessage = 65536;

public:
	void_t Connect()
	{
		while (!::WaitNamedPipeW(pipeName.c_str(), NMPWAIT_WAIT_FOREVER))
		{
			if (const auto error = ::GetLastError(); error != ERROR_FILE_NOT_FOUND) { throw ne::Exception("[MessageQueue/Connect]", std::format("Failed to WaitNamedPipeW function (error: {})", error)); }

			Sleep(1);
		}

		// FILE_FLAG_OVERLAPPED — 진짜 비동기 I/O(IocpEngine 등록)를 쓰려면 핸들 자체가
		// overlapped 로 열려 있어야 한다. Send/Receive(동기 API)도 이 핸들을 그대로 쓰므로
		// 항상 OVERLAPPED 구조체를 넘기고 WaitOverlapped 로 블로킹 대기한다.
		handle = ::CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (handle == INVALID_HANDLE_VALUE) { throw ne::Exception("[MessageQueue/Connect]", std::format("Failed to CreateFileW function (error: {})", ::GetLastError())); }
	}

	void_t Listen()
	{
		handle = ::CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, MaxMessage, MaxMessage, 0, nullptr);
		if (handle == INVALID_HANDLE_VALUE) { throw ne::Exception("[MessageQueue/Listen]", std::format("Failed to CreateNamedPipeW function (error: {})", ::GetLastError())); }

		// ConnectNamedPipe 도 overlapped 핸들에서는 비동기로 완료될 수 있다 — Listen() 자체는
		// 여전히 동기 API 이므로 클라이언트가 붙을 때까지 여기서 블로킹 대기한다.
		OVERLAPPED overlapped{};
		overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!overlapped.hEvent) { throw ne::Exception("[MessageQueue/Listen]", std::format("Failed to CreateEventW function (error: {})", ::GetLastError())); }

		if (!::ConnectNamedPipe(handle, &overlapped))
		{
			if (const ulong_t error = ::GetLastError(); error == ERROR_IO_PENDING)
			{
				ulong_t transferred{};
				if (!::GetOverlappedResult(handle, &overlapped, &transferred, TRUE))
				{
					const ulong_t waitError = ::GetLastError();
					::CloseHandle(overlapped.hEvent);
					throw ne::Exception("[MessageQueue/Listen]", std::format("Failed to GetOverlappedResult function (error: {})", waitError));
				}
			}
			else if (error != ERROR_PIPE_CONNECTED)
			{
				::CloseHandle(overlapped.hEvent);
				throw ne::Exception("[MessageQueue/Listen]", std::format("Failed to ConnectNamedPipe function (error: {})", error));
			}
		}

		::CloseHandle(overlapped.hEvent);
	}

public:
	void_t Send(const std::span<const std::byte> _message) const
	{
		if (isRegistered) throw ne::Exception("[MessageQueue/Send]", "cannot call Send() after SendAsync/ReceiveAsync registered this handle with an IocpEngine — use SendAsync instead");

		OVERLAPPED overlapped{};
		overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!overlapped.hEvent) { throw ne::Exception("[MessageQueue/Send]", std::format("Failed to CreateEventW function (error: {})", ::GetLastError())); }

		if (!::WriteFile(handle, _message.data(), static_cast<ulong_t>(_message.size()), nullptr, &overlapped) && ::GetLastError() != ERROR_IO_PENDING)
		{
			const ulong_t error = ::GetLastError();
			::CloseHandle(overlapped.hEvent);
			throw ne::Exception("[MessageQueue/Send]", std::format("Failed to WriteFile function (error: {})", error));
		}

		ulong_t bytesWritten{};
		WaitOverlapped(overlapped, bytesWritten, "[MessageQueue/Send]");
		::CloseHandle(overlapped.hEvent);
	}

	[[nodiscard]] std::vector<std::byte> Receive() const
	{
		if (isRegistered) throw ne::Exception("[MessageQueue/Receive]", "cannot call Receive() after SendAsync/ReceiveAsync registered this handle with an IocpEngine — use ReceiveAsync instead");

		auto buffer = std::vector<std::byte>(MaxMessage);

		OVERLAPPED overlapped{};
		overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!overlapped.hEvent) { throw ne::Exception("[MessageQueue/Receive]", std::format("Failed to CreateEventW function (error: {})", ::GetLastError())); }

		if (!::ReadFile(handle, buffer.data(), buffer.size(), nullptr, &overlapped) && ::GetLastError() != ERROR_IO_PENDING)
		{
			const ulong_t error = ::GetLastError();
			::CloseHandle(overlapped.hEvent);

			throw ne::Exception("[MessageQueue/Receive]", std::format("Failed to ReadFile function (error: {})", error));
		}

		ulong_t bytesRead{};
		WaitOverlapped(overlapped, bytesRead, "[MessageQueue/Receive]");
		::CloseHandle(overlapped.hEvent);

		buffer.resize(bytesRead);

		return buffer;
	}

public:
	// SendAsync/ReceiveAsync 에서 호출 — 핸들을 IocpEngine 에 최초 1회만 등록한다.
	[[nodiscard]] ne::Result<void_t, ne::OsError> EnsureRegistered(ne::io::IocpEngine& _engine) noexcept
	{
		if (isRegistered) return ne::Result<void_t, ne::OsError>::Ok();

		if (auto result = _engine.RegisterFileHandle(handle); result.IsError()) return result;

		isRegistered = true;

		return ne::Result<void_t, ne::OsError>::Ok();
	}

private:
	// 동기 Send/Receive 가 자신이 제출한 OVERLAPPED 의 완료를 블로킹 대기한다. 오직 이
	// OVERLAPPED 를 대상으로 한 이벤트만 기다리므로, registered 가 false 인 동안(즉
	// 아직 IocpEngine 에 등록되지 않은 동안)에는 RunOnce() 와 경합할 여지가 없다 — 등록 이후엔
	// Send()/Receive() 진입 자체를 위에서 막는다.
	void_t WaitOverlapped(OVERLAPPED& _overlapped, ulong_t& _transferred, const string_view_t _context) const
	{
		if (!::GetOverlappedResult(handle, &_overlapped, &_transferred, TRUE))
		{
			const ulong_t error = ::GetLastError();
			throw ne::Exception(_context, std::format("Failed to GetOverlappedResult function (error: {})", error));
		}
	}
};

#elif defined(IS_POSIX)
class MessageQueue::Impl final
{
public:
	explicit Impl(const string_view_t _name)
		: path(string_t("/tmp/") + string_t(_name) + ".mq.sock") {}
	~Impl() { if (handle != -1) ::close(handle); }

public:
	// SOCK_SEQPACKET 은 mqueue 의 mq_getattr(mq_msgsize) 같은 커널 협상 최대 크기가 없어
	// 애플리케이션 레벨 상한을 둔다. Windows Impl::MaxMessage 와 동일한 값으로 맞춘다.
	static constexpr std::size_t MaxMessage = 65536;

private:
	string_t path;
	int_t handle = -1;

public:
	[[nodiscard]] int_t Handle() const noexcept { return handle; }

public:
	void_t Connect()
	{
		handle = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
		if (handle == -1) { throw ne::Exception("[MessageQueue/Connect]", std::format("Failed to create socket (error: {})", errno)); }

		auto address = sockaddr_un{};
		address.sun_family = AF_UNIX;
		std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

		while (::connect(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
		{
			if (const auto error = errno; error != ENOENT && error != ECONNREFUSED) { throw ne::Exception("[MessageQueue/Connect]", std::format("Failed to connect socket (error: {})", error)); }

			usleep(1000);
		}
	}

	void_t Listen()
	{
		::unlink(path.c_str());

		const auto listenHandle = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
		if (listenHandle == -1) { throw ne::Exception("[MessageQueue/Listen]", std::format("Failed to create socket (error: {})", errno)); }

		auto address = sockaddr_un{};
		address.sun_family = AF_UNIX;
		std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

		if (::bind(listenHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
		{
			const auto error = errno;
			::close(listenHandle);

			throw ne::Exception("[MessageQueue/Listen]", std::format("Failed to bind socket (error: {})", error));
		}

		if (::listen(listenHandle, 1) == -1)
		{
			const auto error = errno;
			::close(listenHandle);

			throw ne::Exception("[MessageQueue/Listen]", std::format("Failed to listen socket (error: {})", error));
		}

		handle = ::accept(listenHandle, nullptr, nullptr);
		const auto acceptError = errno;
		::close(listenHandle);
		::unlink(path.c_str());

		if (handle == -1) { throw ne::Exception("[MessageQueue/Listen]", std::format("Failed to accept socket (error: {})", acceptError)); }
	}

public:
	void_t Send(const std::span<const std::byte> _message) const { if (::send(handle, _message.data(), _message.size(), 0) == -1) { throw ne::Exception("[MessageQueue/Send]", std::format("Failed to send data through socket (error: {})", errno)); } }

	[[nodiscard]] std::vector<std::byte> Receive() const
	{
		auto buffer = std::vector<std::byte>(MaxMessage);

		const auto received = ::recv(handle, buffer.data(), buffer.size(), 0);
		if (received == -1) { throw ne::Exception("[MessageQueue/Receive]", std::format("Failed to receive data through socket (error: {})", errno)); }

		buffer.resize(static_cast<std::size_t>(received));

		return buffer;
	}
};
#endif



MessageQueue::MessageQueue(const string_view_t _name)
	: impl(std::make_unique<Impl>(_name)) {}
MessageQueue::~MessageQueue() = default;

MessageQueue::MessageQueue(MessageQueue&&) noexcept = default;
MessageQueue& MessageQueue::operator=(MessageQueue&&) noexcept = default;



void_t MessageQueue::Connect() { impl->Connect(); }

void_t MessageQueue::Listen() { impl->Listen(); }



void_t MessageQueue::Send(const std::span<const std::byte> _message) const { impl->Send(_message); }

std::vector<std::byte> MessageQueue::Receive() const { return impl->Receive(); }



// ─── 비동기 awaitable ────────────────────────────────────────────────────────
#if defined(_WIN32)
// Windows: 명명 파이프를 FILE_FLAG_OVERLAPPED 로 열어 IocpEngine 에 등록하고,
// ne::io::WriteSubmitAwaitable/ReadSubmitAwaitable(파일 Proactor)로 진짜 완료 기반
// 비동기 I/O 를 수행한다. 파이프는 byte offset 개념이 없으므로 offset 은 항상 0 —
// OVERLAPPED.Offset/OffsetHigh 는 named pipe 에 대해 OS 가 무시한다.
ne::Task<ne::Result<void_t, ne::OsError>> MessageQueue::SendAsync(const std::span<const std::byte> _message, ne::io::IocpEngine& _engine)
{
	if (auto registerResult = impl->EnsureRegistered(_engine); registerResult.IsError()) co_return ne::Result<void_t, ne::OsError>::Error(std::move(registerResult.Error()));

	auto result = co_await ne::io::WriteSubmitAwaitable{ _engine, impl->Handle(), std::span<const ne::byte_t>(reinterpret_cast<const ne::byte_t*>(_message.data()), _message.size()), 0 };
	if (result.IsError()) co_return ne::Result<void_t, ne::OsError>::Error(std::move(result.Error()));

	co_return ne::Result<void_t, ne::OsError>::Ok();
} ne::Task<ne::Result<std::vector<std::byte>, ne::OsError>> MessageQueue::ReceiveAsync(ne::io::IocpEngine& _engine)
{
	if (auto registerResult = impl->EnsureRegistered(_engine); registerResult.IsError()) co_return ne::Result<std::vector<std::byte>, ne::OsError>::Error(std::move(registerResult.Error()));

	auto buffer = std::vector<std::byte>(Impl::MaxMessage);

	auto result = co_await ne::io::ReadSubmitAwaitable{ _engine, impl->Handle(), std::span<ne::byte_t>(reinterpret_cast<ne::byte_t*>(buffer.data()), buffer.size()), 0 };
	if (result.IsError()) co_return ne::Result<std::vector<std::byte>, ne::OsError>::Error(std::move(result.Error()));

	buffer.resize(result.Value());

	co_return ne::Result<std::vector<std::byte>, ne::OsError>::Ok(std::move(buffer));
}

#elif defined(IS_POSIX)
// POSIX: AF_UNIX SOCK_SEQPACKET → IEngine::SubmitSend/SubmitReceive 로 진짜 Proactor
// 제출(IORING_OP_SEND/RECV, epoll 은 Watch+send/recv 로 에뮬레이션) — Reactor 로 준비완료를
// 기다렸다가 별도로 mq_send/mq_receive 를 부르던 이전 2단계 구조가 필요 없다.
ne::Task<ne::Result<void_t, ne::OsError>> MessageQueue::SendAsync(const std::span<const std::byte> _message, ne::io::IEngine& _engine)
{
	auto result = co_await ne::io::SendSubmitAwaitable{ _engine, static_cast<ne::io::socket_t>(impl->Handle()), _message.data(), _message.size() };

	if (result.IsError()) co_return ne::Result<void_t, ne::OsError>::Error(std::move(result.Error()));

	co_return ne::Result<void_t, ne::OsError>::Ok();
} ne::Task<ne::Result<std::vector<std::byte>, ne::OsError>> MessageQueue::ReceiveAsync(ne::io::IEngine& _engine)
{
	auto buffer = std::vector<std::byte>(Impl::MaxMessage);
	auto result = co_await ne::io::ReceiveSubmitAwaitable{ _engine, static_cast<ne::io::socket_t>(impl->Handle()), buffer.data(), buffer.size() };

	if (result.IsError()) co_return ne::Result<std::vector<std::byte>, ne::OsError>::Error(std::move(result.Error()));

	buffer.resize(result.Value());
	co_return ne::Result<std::vector<std::byte>, ne::OsError>::Ok(std::move(buffer));
}

#else
ne::Task<ne::Result<void_t, ne::OsError>> MessageQueue::SendAsync(const std::span<const std::byte>, ne::io::IEngine&) { co_return ne::Result<void_t, ne::OsError>::Error(ne::OsError{ 0, "[MessageQueue/SendAsync] not supported on this platform" }); }

ne::Task<ne::Result<std::vector<std::byte>, ne::OsError>> MessageQueue::ReceiveAsync(ne::io::IEngine&) { co_return ne::Result<std::vector<std::byte>, ne::OsError>::Error(ne::OsError{ 0, "[MessageQueue/ReceiveAsync] not supported on this platform" }); }

#endif

END_NS
