//
// Created by nebula on 24. 5. 29.
//

#include "Pipe.h"

#include <thread>
#include "Exception.h"
#include "StringFormat.h"

#if defined(_WIN32)
#	include <windows.h>
#elif defined(IS_POSIX)
#	include <sys/socket.h>
#	include <sys/un.h>
#	include <unistd.h>
#	include <cerrno>
#	include <cstring>
#endif

BEGIN_NS(ne::protocol::Ipc)
#if defined(_WIN32)
	class Pipe::Impl final
	{
	public:
		explicit Impl(const string_view_t _name)
			: pipeName(LR"(\\.\pipe\)" + StringFormat::UTF8toWCS(string_t(_name).c_str()))
		{
		}
		~Impl()
		{
			if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
		}

	private:
		wstring_t pipeName;
		HANDLE handle = INVALID_HANDLE_VALUE;

	public:
		void_t Listen()
		{
			handle = ::CreateNamedPipeW(
				pipeName.c_str(),
				PIPE_ACCESS_DUPLEX,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
				1,
				DefaultBufferSize,
				DefaultBufferSize,
				0,
				nullptr
			);
			if (handle == INVALID_HANDLE_VALUE)
			{
				throw ne::Exception("[Pipe/Listen]", std::format("Failed to CreateNamedPipeW function (error: {})", ::GetLastError()));
			}

			if (!::ConnectNamedPipe(handle, nullptr) && ::GetLastError() != ERROR_PIPE_CONNECTED)
			{
				throw ne::Exception("[Pipe/Listen]", std::format("Failed to ConnectNamedPipe function (error: {})", ::GetLastError()));
			}
		}

		void_t Connect()
		{
			while (!::WaitNamedPipeW(pipeName.c_str(), NMPWAIT_WAIT_FOREVER))
			{
				if (const auto error = ::GetLastError(); error != ERROR_FILE_NOT_FOUND)
				{
					throw ne::Exception("[Pipe/Connect]", std::format("Failed to WaitNamedPipeW function (error: {})", error));
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			handle = ::CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				throw ne::Exception("[Pipe/Connect]", std::format("Failed to CreateFileW function (error: {})", ::GetLastError()));
			}
		}

		[[nodiscard]] bool_t IsConnected() const noexcept
		{
			return handle != INVALID_HANDLE_VALUE;
		}

		[[nodiscard]] longlong_t Read(const std::span<std::byte> _buffer) const
		{
			auto bytesRead = DWORD{};
			if (!::ReadFile(handle, _buffer.data(), static_cast<DWORD>(_buffer.size()), &bytesRead, nullptr))
			{
				if (const auto error = ::GetLastError(); error == ERROR_BROKEN_PIPE) return -1;
				else throw ne::Exception("[Pipe/Read]", std::format("Failed to ReadFile function (error: {})", error));
			}

			return static_cast<longlong_t>(bytesRead);
		}

		bool_t Write(const std::span<const std::byte> _data) const
		{
			auto bytesWritten = DWORD{};
			if (!::WriteFile(handle, _data.data(), static_cast<DWORD>(_data.size()), &bytesWritten, nullptr))
			{
				throw ne::Exception("[Pipe/Write]", std::format("Failed to WriteFile function (error: {})", ::GetLastError()));
			}

			return true;
		}

	private:
		static constexpr DWORD DefaultBufferSize = 4096;
	};
#elif defined(IS_POSIX)
	class Pipe::Impl final
	{
	public:
		explicit Impl(const string_view_t _name)
			: path(string_t("/tmp/") + string_t(_name) + ".sock")
		{
		}
		~Impl()
		{
			if (handle != -1) ::close(handle);
		}

	private:
		string_t path;
		int_t handle = -1;

	public:
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
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		[[nodiscard]] bool_t IsConnected() const noexcept
		{
			return handle != -1;
		}

		[[nodiscard]] longlong_t Read(const std::span<std::byte> _buffer) const
		{
			const auto result = ::recv(handle, _buffer.data(), _buffer.size(), 0);
			if (result < 0)
			{
				throw ne::Exception("[Pipe/Read]", std::format("Failed to receive data through socket (error: {})", errno));
			}
			if (result == 0) return -1;

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
	};
#endif



	Pipe::Pipe(const string_view_t _name)
		: impl(std::make_unique<Impl>(_name))
	{
	}
	Pipe::~Pipe() = default;

	Pipe::Pipe(Pipe&&) noexcept = default;
	Pipe& Pipe::operator=(Pipe&&) noexcept = default;



	void_t Pipe::Listen()
	{
		impl->Listen();
	}

	void_t Pipe::Connect()
	{
		impl->Connect();
	}

	bool_t Pipe::IsConnected() const noexcept
	{
		return impl->IsConnected();
	}

	longlong_t Pipe::Read(const std::span<std::byte> _buffer) const
	{
		return impl->Read(_buffer);
	}

	bool_t Pipe::Write(const std::span<const std::byte> _data) const
	{
		return impl->Write(_data);
	}

END_NS
