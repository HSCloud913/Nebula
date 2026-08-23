//
// Created by nebula on 24. 5. 29.
//

#include "Ipc/Pipe.h"

#include "Base/Exception.h"
#include "Util/StringFormat.h"
#include "Io/Engine.h"
#include "Io/Context.h"
#include "Io/Coroutine/IoOperation.h"

#if defined(IS_POSIX)
#	include <sys/socket.h>
#	include <sys/un.h>
#	include <unistd.h>
#	include <cerrno>
#	include <cstring>
#endif



namespace ne::ipc
{
#if defined(_WIN32)
class Pipe::Impl final
{
public:
	explicit Impl(const string_view_t _name)
		: pipeName(LR"(\\.\pipe\)" + ne::util::StringFormat::UTF8toWCS(string_t(_name).c_str())) {}
	~Impl() { if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle); }

private:
	static constexpr ulong_t DefaultBufferSize = 4096;

private:
	wstring_t pipeName;
	HANDLE handle = INVALID_HANDLE_VALUE;
	bool_t isAsyncUsed{ false }; // ReadAsync/WriteAsync 를 쓴 뒤로는 완료가 IOCP 큐로 가므로 동기 경로를 막는다



public:
	void_t Connect()
	{
		while (!::WaitNamedPipeW(pipeName.c_str(), NMPWAIT_WAIT_FOREVER))
		{
			if (const auto error = ::GetLastError(); error != ERROR_FILE_NOT_FOUND) { throw ne::Exception("[Pipe/Connect]", std::format("Failed to WaitNamedPipeW function (error: {})", error)); }

			Sleep(1);
		}

		// FILE_FLAG_OVERLAPPED — 진짜 비동기 I/O(IocpEngine 등록)를 쓰려면 핸들 자체가
		// overlapped 로 열려 있어야 한다. Read/Write(동기 API)도 이 핸들을 그대로 쓰므로
		// 항상 OVERLAPPED 구조체를 넘기고 GetOverlappedResult 로 블로킹 대기한다.
		handle = ::CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (handle == INVALID_HANDLE_VALUE) { throw ne::Exception("[Pipe/Connect]", std::format("Failed to CreateFileW function (error: {})", ::GetLastError())); }
	}

	void_t Listen()
	{
		handle = ::CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, DefaultBufferSize, DefaultBufferSize, 0, nullptr);
		if (handle == INVALID_HANDLE_VALUE) { throw ne::Exception("[Pipe/Listen]", std::format("Failed to CreateNamedPipeW function (error: {})", ::GetLastError())); }

		// ConnectNamedPipe 도 overlapped 핸들에서는 비동기로 완료될 수 있다 — Listen() 자체는
		// 여전히 동기 API 이므로 클라이언트가 붙을 때까지 여기서 블로킹 대기한다.
		OVERLAPPED overlapped{};
		overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!overlapped.hEvent) { throw ne::Exception("[Pipe/Listen]", std::format("Failed to CreateEventW function (error: {})", ::GetLastError())); }

		if (!::ConnectNamedPipe(handle, &overlapped))
		{
			const ulong_t error = ::GetLastError();
			if (error == ERROR_IO_PENDING)
			{
				ulong_t transferred{};
				if (!::GetOverlappedResult(handle, &overlapped, &transferred, TRUE))
				{
					const ulong_t waitError = ::GetLastError();
					::CloseHandle(overlapped.hEvent);

					throw ne::Exception("[Pipe/Listen]", std::format("Failed to GetOverlappedResult function (error: {})", waitError));
				}
			}
			else if (error != ERROR_PIPE_CONNECTED)
			{
				::CloseHandle(overlapped.hEvent);

				throw ne::Exception("[Pipe/Listen]", std::format("Failed to ConnectNamedPipe function (error: {})", error));
			}
		}

		::CloseHandle(overlapped.hEvent);
	}

public:
	[[nodiscard]] longlong_t Read(const std::span<std::byte> _buffer) const
	{
		if (isAsyncUsed) throw ne::Exception("[Pipe/Read]", "cannot call Read() after ReadAsync/WriteAsync — this handle now delivers completions to the IOCP queue; use ReadAsync instead");

		OVERLAPPED overlapped{};
		overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!overlapped.hEvent) { throw ne::Exception("[Pipe/Read]", std::format("Failed to CreateEventW function (error: {})", ::GetLastError())); }

		if (!::ReadFile(handle, _buffer.data(), static_cast<ulong_t>(_buffer.size()), nullptr, &overlapped))
		{
			if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING)
			{
				::CloseHandle(overlapped.hEvent);
				if (error == ERROR_BROKEN_PIPE) return -1;

				throw ne::Exception("[Pipe/Read]", std::format("Failed to ReadFile function (error: {})", error));
			}
		}

		ulong_t bytesRead{};
		if (!::GetOverlappedResult(handle, &overlapped, &bytesRead, TRUE))
		{
			const ulong_t error = ::GetLastError();
			::CloseHandle(overlapped.hEvent);
			if (error == ERROR_BROKEN_PIPE) return -1;

			throw ne::Exception("[Pipe/Read]", std::format("Failed to GetOverlappedResult function (error: {})", error));
		}

		::CloseHandle(overlapped.hEvent);

		return static_cast<longlong_t>(bytesRead);
	}

	bool_t Write(const std::span<const std::byte> _data) const
	{
		if (isAsyncUsed) throw ne::Exception("[Pipe/Write]", "cannot call Write() after ReadAsync/WriteAsync — this handle now delivers completions to the IOCP queue; use WriteAsync instead");

		OVERLAPPED overlapped{};
		overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!overlapped.hEvent) { throw ne::Exception("[Pipe/Write]", std::format("Failed to CreateEventW function (error: {})", ::GetLastError())); }

		if (!::WriteFile(handle, _data.data(), static_cast<ulong_t>(_data.size()), nullptr, &overlapped) && ::GetLastError() != ERROR_IO_PENDING)
		{
			const ulong_t error = ::GetLastError();
			::CloseHandle(overlapped.hEvent);

			throw ne::Exception("[Pipe/Write]", std::format("Failed to WriteFile function (error: {})", error));
		}

		ulong_t bytesWritten{};
		if (!::GetOverlappedResult(handle, &overlapped, &bytesWritten, TRUE))
		{
			const ulong_t error = ::GetLastError();
			::CloseHandle(overlapped.hEvent);

			throw ne::Exception("[Pipe/Write]", std::format("Failed to GetOverlappedResult function (error: {})", error));
		}

		::CloseHandle(overlapped.hEvent);

		return true;
	}

public:
	// ReadAsync/WriteAsync 에서 호출 — 이 핸들이 이제 IOCP 로 완료를 받는다는 사실만 기록한다.
	// IOCP 연관(CreateIoCompletionPort)은 엔진이 첫 제출에서 알아서 하고 멱등하므로, 예전처럼
	// 여기서 등록을 대신할 필요가 없다. 남은 목적은 아래 동기 Read/Write 를 막는 것 하나다.
	void_t MarkAsyncUsed() noexcept { isAsyncUsed = true; }

	[[nodiscard]] ne::ulonglong_t HandleValue() const noexcept { return reinterpret_cast<ne::ulonglong_t>(handle); }
	[[nodiscard]] bool_t IsConnected() const noexcept { return handle != INVALID_HANDLE_VALUE; }
};

#elif defined(IS_POSIX)
class Pipe::Impl final
{
public:
	explicit Impl(const string_view_t _name)
		: path(string_t("/tmp/") + string_t(_name) + ".sock") {}
	~Impl() { if (handle != -1) ::close(handle); }

private:
	string_t path;
	int_t handle = -1;

public:
	void_t Connect()
	{
		handle = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (handle == -1) { throw ne::Exception("[Pipe/Connect]", std::format("Failed to create socket (error: {})", errno)); }

		auto address = sockaddr_un{};
		address.sun_family = AF_UNIX;
		std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

		while (::connect(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
		{
			if (const auto error = errno; error != ENOENT && error != ECONNREFUSED) { throw ne::Exception("[Pipe/Connect]", std::format("Failed to connect socket (error: {})", error)); }

			usleep(1000);
		}
	}

	void_t Listen()
	{
		::unlink(path.c_str());

		const auto listenHandle = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (listenHandle == -1) { throw ne::Exception("[Pipe/Listen]", std::format("Failed to create socket (error: {})", errno)); }

		auto address = sockaddr_un{};
		address.sun_family = AF_UNIX;
		std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

		if (::bind(listenHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
		{
			const auto error = errno;
			::close(listenHandle);
			throw ne::Exception("[Pipe/Listen]", std::format("Failed to bind socket (error: {})", error));
		}

		if (::listen(listenHandle, 1) == -1)
		{
			const auto error = errno;
			::close(listenHandle);
			throw ne::Exception("[Pipe/Listen]", std::format("Failed to listen socket (error: {})", error));
		}

		handle = ::accept(listenHandle, nullptr, nullptr);
		const auto acceptError = errno;
		::close(listenHandle);
		::unlink(path.c_str());

		if (handle == -1) { throw ne::Exception("[Pipe/Listen]", std::format("Failed to accept socket (error: {})", acceptError)); }
	}

public:
	[[nodiscard]] longlong_t Read(const std::span<std::byte> _buffer) const
	{
		const auto result = ::recv(handle, _buffer.data(), _buffer.size(), 0);
		if (result < 0) { throw ne::Exception("[Pipe/Read]", std::format("Failed to receive data through socket (error: {})", errno)); }
		if (result == 0) { return -1; }

		return static_cast<longlong_t>(result);
	}

	bool_t Write(const std::span<const std::byte> _data) const
	{
		if (::send(handle, _data.data(), _data.size(), 0) == -1) { throw ne::Exception("[Pipe/Write]", std::format("Failed to send data through socket (error: {})", errno)); }

		return true;
	}

public:
	// POSIX 는 동기/비동기를 섞어도 되므로(같은 fd 에 recv 와 io_uring 제출이 공존 가능) 아무것도
	// 기록할 필요가 없다. Windows Impl 과 표면을 맞추기 위해 no-op 으로만 둔다.
	void_t MarkAsyncUsed() noexcept {}

	[[nodiscard]] ne::ulonglong_t HandleValue() const noexcept { return static_cast<ne::ulonglong_t>(handle); }
	[[nodiscard]] bool_t IsConnected() const noexcept { return handle != -1; }
};

#endif



Pipe::Pipe(const string_view_t _name)
	: impl(std::make_unique<Impl>(_name)) {}
Pipe::~Pipe() = default;

Pipe::Pipe(Pipe&&) noexcept = default;
Pipe& Pipe::operator=(Pipe&&) noexcept = default;



void_t Pipe::Connect() { impl->Connect(); }

void_t Pipe::Listen() { impl->Listen(); }



longlong_t Pipe::Read(const std::span<std::byte> _buffer) const { return impl->Read(_buffer); }

bool_t Pipe::Write(const std::span<const std::byte> _data) const { return impl->Write(_data); }



// ─── 비동기 API ──────────────────────────────────────────────────────────────
// 플랫폼 차이는 "어떤 RequestKind 를 쓰는가" 하나로 줄어든다. Windows 는 명명 파이프 HANDLE 에
// READ/WRITE(ReadFile/WriteFile + OVERLAPPED), POSIX 는 AF_UNIX 소켓에 RECEIVE/SEND 를 제출한다.
// 파이프에는 byte offset 개념이 없어 offset 은 항상 0 — 명명 파이프에 대해 OS 가 무시한다.
#if defined(_WIN32)
namespace
{
	constexpr ne::io::RequestKind PipeReadKind = ne::io::RequestKind::READ;
	constexpr ne::io::RequestKind PipeWriteKind = ne::io::RequestKind::WRITE;
}
#elif defined(IS_POSIX)
namespace
{
	constexpr ne::io::RequestKind PipeReadKind = ne::io::RequestKind::RECEIVE;
	constexpr ne::io::RequestKind PipeWriteKind = ne::io::RequestKind::SEND;
}
#endif

#if defined(_WIN32) || defined(IS_POSIX)
ne::Task<ne::io::IoResult<std::size_t>> Pipe::ReadAsync(const std::span<std::byte> _buffer, ne::io::Context& _context, std::stop_token _stopToken)
{
	impl->MarkAsyncUsed();

	const ne::io::Request request{ .requestKind = PipeReadKind, .handle = impl->HandleValue(), .buffer = _buffer.data(), .length = _buffer.size() };

	co_return co_await ne::io::IoOperation{ _context, request, std::move(_stopToken) };
}

ne::Task<ne::io::IoResult<std::size_t>> Pipe::WriteAsync(const std::span<const std::byte> _data, ne::io::Context& _context, std::stop_token _stopToken)
{
	impl->MarkAsyncUsed();

	// Request::buffer 는 방향을 구분하지 않는 void_t* 다(엔진이 RequestKind 로 방향을 안다) —
	// Io/File::Write 도 같은 이유로 const 를 벗긴다.
	const ne::io::Request request{ .requestKind = PipeWriteKind, .handle = impl->HandleValue(), .buffer = const_cast<std::byte*>(_data.data()), .length = _data.size() };

	co_return co_await ne::io::IoOperation{ _context, request, std::move(_stopToken) };
}

#else
ne::Task<ne::io::IoResult<std::size_t>> Pipe::ReadAsync(const std::span<std::byte>, ne::io::Context&, std::stop_token) { co_return ne::io::IoResult<std::size_t>::Error(ne::io::IoError{ ne::io::IoErrorKind::UNSUPPORTED, "[Pipe/ReadAsync] not supported on this platform" }); }

ne::Task<ne::io::IoResult<std::size_t>> Pipe::WriteAsync(const std::span<const std::byte>, ne::io::Context&, std::stop_token) { co_return ne::io::IoResult<std::size_t>::Error(ne::io::IoError{ ne::io::IoErrorKind::UNSUPPORTED, "[Pipe/WriteAsync] not supported on this platform" }); }
#endif



bool_t Pipe::IsConnected() const noexcept { return impl->IsConnected(); }

}
