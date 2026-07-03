//
// Created by nebula on 24. 5. 29.
//

#include "Pipe.h"

#include "Exception.h"
#include "StringFormat.h"
#include "Engine/IIoEngine.h"
#include "Coroutine/Awaitable.h"

#if defined(_WIN32)
#	include <windows.h>
#elif defined(IS_POSIX)
#	include <sys/socket.h>
#	include <sys/un.h>
#	include <unistd.h>
#	include <cerrno>
#	include <cstring>
#endif



BEGIN_NS(ne::ipc)
#if defined(_WIN32)
	class Pipe::Impl final
	{
	public:
		explicit Impl(const string_view_t _name)
			: pipeName(LR"(\\.\pipe\)" + StringFormat::UTF8toWCS(string_t(_name).c_str())) {}
		~Impl() { if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle); }

	private:
		static constexpr ulong_t DefaultBufferSize = 4096;

	private:
		wstring_t pipeName;
		HANDLE handle = INVALID_HANDLE_VALUE;
		bool_t registered{ false }; // IocpEngine 등록 여부 — 최초 ReadAsync/WriteAsync 에서 1회만 등록(RegisterFileHandle 은 멱등하지 않음)



	public:
		void_t Connect()
		{
			while (!::WaitNamedPipeW(pipeName.c_str(), NMPWAIT_WAIT_FOREVER))
			{
				if (const auto error = ::GetLastError(); error != ERROR_FILE_NOT_FOUND)
				{
					throw ne::Exception("[Pipe/Connect]", std::format("Failed to WaitNamedPipeW function (error: {})", error));
				}

				Sleep(1);
			}

			// FILE_FLAG_OVERLAPPED — 진짜 비동기 I/O(IocpEngine 등록)를 쓰려면 핸들 자체가
			// overlapped 로 열려 있어야 한다. Read/Write(동기 API)도 이 핸들을 그대로 쓰므로
			// 항상 OVERLAPPED 구조체를 넘기고 GetOverlappedResult 로 블로킹 대기한다.
			handle = ::CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				throw ne::Exception("[Pipe/Connect]", std::format("Failed to CreateFileW function (error: {})", ::GetLastError()));
			}
		}

		void_t Listen()
		{
			handle = ::CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, DefaultBufferSize, DefaultBufferSize, 0, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				throw ne::Exception("[Pipe/Listen]", std::format("Failed to CreateNamedPipeW function (error: {})", ::GetLastError()));
			}

			// ConnectNamedPipe 도 overlapped 핸들에서는 비동기로 완료될 수 있다 — Listen() 자체는
			// 여전히 동기 API 이므로 클라이언트가 붙을 때까지 여기서 블로킹 대기한다.
			OVERLAPPED overlapped{};
			overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!overlapped.hEvent)
			{
				throw ne::Exception("[Pipe/Listen]", std::format("Failed to CreateEventW function (error: {})", ::GetLastError()));
			}

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
			if (registered)
				throw ne::Exception("[Pipe/Read]", "cannot call Read() after ReadAsync/WriteAsync registered this handle with an IocpEngine — use ReadAsync instead");

			OVERLAPPED overlapped{};
			overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!overlapped.hEvent)
			{
				throw ne::Exception("[Pipe/Read]", std::format("Failed to CreateEventW function (error: {})", ::GetLastError()));
			}

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
			if (registered)
				throw ne::Exception("[Pipe/Write]", "cannot call Write() after ReadAsync/WriteAsync registered this handle with an IocpEngine — use WriteAsync instead");

			OVERLAPPED overlapped{};
			overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!overlapped.hEvent)
			{
				throw ne::Exception("[Pipe/Write]", std::format("Failed to CreateEventW function (error: {})", ::GetLastError()));
			}

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
		// ReadAsync/WriteAsync 에서 호출 — 핸들을 IocpEngine 에 최초 1회만 등록한다.
		[[nodiscard]] ne::Result<void, ne::OsError> EnsureRegistered(ne::io::IocpEngine& _engine) noexcept
		{
			if (registered) return ne::Result<void, ne::OsError>::Ok();

			if (auto result = _engine.RegisterFileHandle(handle); result.IsError())
				return result;

			registered = true;

			return ne::Result<void, ne::OsError>::Ok();
		}

	public:
		[[nodiscard]] HANDLE Handle() const noexcept { return handle; }
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
			if (handle == -1)
			{
				throw ne::Exception("[Pipe/Connect]", std::format("Failed to create socket (error: {})", errno));
			}

			auto address = sockaddr_un{};
			address.sun_family = AF_UNIX;
			std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

			while (::connect(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
			{
				if (const auto error = errno; error != ENOENT && error != ECONNREFUSED)
				{
					throw ne::Exception("[Pipe/Connect]", std::format("Failed to connect socket (error: {})", error));
				}

				usleep(1000);
			}
		}

		void_t Listen()
		{
			::unlink(path.c_str());

			const auto listenHandle = ::socket(AF_UNIX, SOCK_STREAM, 0);
			if (listenHandle == -1)
			{
				throw ne::Exception("[Pipe/Listen]", std::format("Failed to create socket (error: {})", errno));
			}

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

			if (handle == -1)
			{
				throw ne::Exception("[Pipe/Listen]", std::format("Failed to accept socket (error: {})", acceptError));
			}
		}

	public:
		[[nodiscard]] longlong_t Read(const std::span<std::byte> _buffer) const
		{
			const auto result = ::recv(handle, _buffer.data(), _buffer.size(), 0);
			if (result < 0)
			{
				throw ne::Exception("[Pipe/Read]", std::format("Failed to receive data through socket (error: {})", errno));
			}
			if (result == 0)
			{
				return -1;
			}

			return static_cast<longlong_t>(result);
		}

		bool_t Write(const std::span<const std::byte> _data) const
		{
			if (::send(handle, _data.data(), _data.size(), 0) == -1)
			{
				throw ne::Exception("[Pipe/Write]", std::format("Failed to send data through socket (error: {})", errno));
			}

			return true;
		}

	public:
		[[nodiscard]] int_t Handle() const noexcept { return handle; }
		[[nodiscard]] bool_t IsConnected() const noexcept { return handle != -1; }
	};

#endif



	Pipe::Pipe(const string_view_t _name)
		: impl(std::make_unique<Impl>(_name)) {}
	Pipe::~Pipe() = default;

	Pipe::Pipe(Pipe&&) noexcept = default;
	Pipe& Pipe::operator=(Pipe&&) noexcept = default;



	void_t Pipe::Connect()
	{
		impl->Connect();
	}

	void_t Pipe::Listen()
	{
		impl->Listen();
	}



	longlong_t Pipe::Read(const std::span<std::byte> _buffer) const
	{
		return impl->Read(_buffer);
	}

	bool_t Pipe::Write(const std::span<const std::byte> _data) const
	{
		return impl->Write(_data);
	}



	// ─── 비동기 API ──────────────────────────────────────────────────────────────
#if defined(_WIN32)
	// Windows: 명명 파이프를 FILE_FLAG_OVERLAPPED 로 열어 IocpEngine 에 등록하고,
	// ne::io::ReadSubmitAwaitable/WriteSubmitAwaitable(파일 Proactor)로 진짜 완료 기반
	// 비동기 I/O 를 수행한다. 파이프는 byte offset 개념이 없으므로 offset 은 항상 0.
	ne::Task<ne::Result<std::size_t, ne::OsError>> Pipe::ReadAsync(const std::span<std::byte> _buffer, ne::io::IocpEngine& _engine)
	{
		if (auto registerResult = impl->EnsureRegistered(_engine); registerResult.IsError())
			co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(registerResult.Error()));

		co_return co_await ne::io::ReadSubmitAwaitable{_engine, impl->Handle(), std::span<ne::byte_t>(reinterpret_cast<ne::byte_t*>(_buffer.data()), _buffer.size()), 0};
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> Pipe::WriteAsync(const std::span<const std::byte> _data, ne::io::IocpEngine& _engine)
	{
		if (auto registerResult = impl->EnsureRegistered(_engine); registerResult.IsError())
			co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(registerResult.Error()));

		co_return co_await ne::io::WriteSubmitAwaitable{_engine, impl->Handle(), std::span<const ne::byte_t>(reinterpret_cast<const ne::byte_t*>(_data.data()), _data.size()), 0};
	}

#elif defined(IS_POSIX)
	// POSIX: AF_UNIX SOCK_STREAM → IIoEngine::SubmitReceive/SubmitSend 로 진짜 Proactor 제출
	// (IORING_OP_RECV/SEND, epoll 은 Watch+recv/send 로 에뮬레이션).
	ne::Task<ne::Result<std::size_t, ne::OsError>> Pipe::ReadAsync(const std::span<std::byte> _buffer, ne::io::IIoEngine& _engine)
	{
		co_return co_await ne::io::ReceiveSubmitAwaitable{_engine, static_cast<ne::io::socket_t>(impl->Handle()), _buffer.data(), _buffer.size()};
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> Pipe::WriteAsync(const std::span<const std::byte> _data, ne::io::IIoEngine& _engine)
	{
		co_return co_await ne::io::SendSubmitAwaitable{_engine, static_cast<ne::io::socket_t>(impl->Handle()), _data.data(), _data.size()};
	}

#else
	ne::Task<ne::Result<std::size_t, ne::OsError>> Pipe::ReadAsync(const std::span<std::byte>, ne::io::IIoEngine&)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(
			ne::OsError{ 0, "[Pipe/ReadAsync] not supported on this platform" });
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> Pipe::WriteAsync(const std::span<const std::byte>, ne::io::IIoEngine&)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(
			ne::OsError{ 0, "[Pipe/WriteAsync] not supported on this platform" });
	}
#endif



	bool_t Pipe::IsConnected() const noexcept
	{
		return impl->IsConnected();
	}

END_NS
