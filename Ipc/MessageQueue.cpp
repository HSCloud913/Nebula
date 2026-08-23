//
// Created by nebula on 24. 5. 29.
//

#include "Ipc/MessageQueue.h"

#include "Base/Exception.h"
#include "Util/StringFormat.h"
#include "Io/Engine.h"
#include "Io/Context.h"
#include "Io/Coroutine/IoOperation.h"

#if defined(_WIN32)
#	include "Base/WinsockApi.h"
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



namespace ne::ipc
{
#if defined(_WIN32)
class MessageQueue::Impl final
{
public:
	explicit Impl(const string_view_t _name)
		: pipeName(LR"(\\.\pipe\)" + ne::util::StringFormat::UTF8toWCS(string_t(_name).c_str())) {}
	~Impl() { if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle); }

private:
	wstring_t pipeName;
	HANDLE handle = INVALID_HANDLE_VALUE;
	bool_t isAsyncUsed{ false }; // SendAsync/ReceiveAsync 를 쓴 뒤로는 완료가 IOCP 큐로 가므로 동기 경로를 막는다

public:
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
		if (isAsyncUsed) throw ne::Exception("[MessageQueue/Send]", "cannot call Send() after SendAsync/ReceiveAsync — this handle now delivers completions to the IOCP queue; use SendAsync instead");

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
		if (isAsyncUsed) throw ne::Exception("[MessageQueue/Receive]", "cannot call Receive() after SendAsync/ReceiveAsync — this handle now delivers completions to the IOCP queue; use ReceiveAsync instead");

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
	// SendAsync/ReceiveAsync 에서 호출 — 이 핸들이 이제 IOCP 로 완료를 받는다는 사실만 기록한다.
	// IOCP 연관(CreateIoCompletionPort)은 엔진이 첫 제출에서 알아서 하고 멱등하므로, 예전처럼
	// 여기서 등록을 대신할 필요가 없다. 남은 목적은 위 동기 Send/Receive 를 막는 것 하나다.
	void_t MarkAsyncUsed() noexcept { isAsyncUsed = true; }

	[[nodiscard]] ne::ulonglong_t HandleValue() const noexcept { return reinterpret_cast<ne::ulonglong_t>(handle); }

private:
	// 동기 Send/Receive 가 자신이 제출한 OVERLAPPED 의 완료를 블로킹 대기한다. 오직 이
	// OVERLAPPED 를 대상으로 한 이벤트만 기다리므로, isAsyncUsed 가 false 인 동안(즉 이 핸들이
	// 아직 IOCP 에 연관되지 않은 동안)에는 RunOnce() 와 경합할 여지가 없다 — 비동기 API 를 한 번
	// 쓴 뒤에는 Send()/Receive() 진입 자체를 위에서 막는다.
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
	// POSIX 는 동기/비동기를 섞어도 되므로 기록할 것이 없다 — Windows Impl 과 표면만 맞춘다.
	void_t MarkAsyncUsed() noexcept {}

	[[nodiscard]] ne::ulonglong_t HandleValue() const noexcept { return static_cast<ne::ulonglong_t>(handle); }

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



// ─── 비동기 API ──────────────────────────────────────────────────────────────
// 플랫폼 차이는 RequestKind 하나로 줄어든다. Windows 는 명명 파이프 HANDLE 에 READ/WRITE
// (ReadFile/WriteFile + OVERLAPPED), POSIX 는 AF_UNIX SOCK_SEQPACKET 소켓에 RECEIVE/SEND 를
// 제출한다. 메시지 경계는 양쪽 모두 OS 가 보존한다(PIPE_READMODE_MESSAGE / SOCK_SEQPACKET) —
// 그래서 한 번의 완료가 정확히 한 메시지다. 파이프에는 offset 개념이 없어 offset 은 항상 0.
#if defined(_WIN32)
namespace
{
	constexpr ne::io::RequestKind QueueReceiveKind = ne::io::RequestKind::READ;
	constexpr ne::io::RequestKind QueueSendKind = ne::io::RequestKind::WRITE;
}
#elif defined(IS_POSIX)
namespace
{
	constexpr ne::io::RequestKind QueueReceiveKind = ne::io::RequestKind::RECEIVE;
	constexpr ne::io::RequestKind QueueSendKind = ne::io::RequestKind::SEND;
}
#endif

#if defined(_WIN32) || defined(IS_POSIX)
ne::Task<ne::io::IoResult<void_t>> MessageQueue::SendAsync(const std::span<const std::byte> _message, ne::io::Context& _context, std::stop_token _stopToken)
{
	impl->MarkAsyncUsed();

	// Request::buffer 는 방향을 구분하지 않는 void_t* 다(엔진이 RequestKind 로 방향을 안다).
	const ne::io::Request request{ .requestKind = QueueSendKind, .handle = impl->HandleValue(), .buffer = const_cast<std::byte*>(_message.data()), .length = _message.size() };

	auto result = co_await ne::io::IoOperation{ _context, request, std::move(_stopToken) };
	if (result.IsError()) co_return ne::io::IoResult<void_t>::Error(std::move(result.Error()));

	co_return ne::io::IoResult<void_t>::Ok();
}

ne::Task<ne::io::IoResult<std::vector<std::byte>>> MessageQueue::ReceiveAsync(ne::io::Context& _context, std::stop_token _stopToken)
{
	using R = ne::io::IoResult<std::vector<std::byte>>;

	impl->MarkAsyncUsed();

	// 메시지 경계가 보존되므로 한 메시지가 다 들어갈 버퍼를 미리 잡아야 한다 — 작게 잡으면
	// Windows 는 ERROR_MORE_DATA, POSIX SOCK_SEQPACKET 은 **나머지를 조용히 버린다.**
	auto buffer = std::vector<std::byte>(Impl::MaxMessage);

	// 버퍼는 이 코루틴 프레임이 소유한다. IoOperation 이 파괴될 때 커널 취소를 요청하는 이유가
	// 이것이다 — 취소 없이 프레임만 사라지면 커널이 이 벡터에 계속 쓴다.
	const ne::io::Request request{ .requestKind = QueueReceiveKind, .handle = impl->HandleValue(), .buffer = buffer.data(), .length = buffer.size() };

	auto result = co_await ne::io::IoOperation{ _context, request, std::move(_stopToken) };
	if (result.IsError()) co_return R::Error(std::move(result.Error()));

	buffer.resize(result.Value());

	co_return R::Ok(std::move(buffer));
}

#else
ne::Task<ne::io::IoResult<void_t>> MessageQueue::SendAsync(const std::span<const std::byte>, ne::io::Context&, std::stop_token) { co_return ne::io::IoResult<void_t>::Error(ne::io::IoError{ ne::io::IoErrorKind::UNSUPPORTED, "[MessageQueue/SendAsync] not supported on this platform" }); }

ne::Task<ne::io::IoResult<std::vector<std::byte>>> MessageQueue::ReceiveAsync(ne::io::Context&, std::stop_token) { co_return ne::io::IoResult<std::vector<std::byte>>::Error(ne::io::IoError{ ne::io::IoErrorKind::UNSUPPORTED, "[MessageQueue/ReceiveAsync] not supported on this platform" }); }

#endif

}
