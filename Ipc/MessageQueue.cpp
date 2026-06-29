//
// Created by nebula on 24. 5. 29.
//

#include "MessageQueue.h"

#include <thread>
#include "Exception.h"
#include "StringFormat.h"

#if defined(_WIN32)
#	include <windows.h>
#elif defined(IS_POSIX)
#	include <mqueue.h>
#	include <fcntl.h>
#	include <cerrno>
#endif

BEGIN_NS(ne::protocol::Ipc)
#if defined(_WIN32)
	class MessageQueue::Impl final
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
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
				1,
				MaxMessageSize,
				MaxMessageSize,
				0,
				nullptr
			);
			if (handle == INVALID_HANDLE_VALUE)
			{
				throw ne::Exception("[MessageQueue/Listen]", std::format("Failed to CreateNamedPipeW function (error: {})", ::GetLastError()));
			}

			if (!::ConnectNamedPipe(handle, nullptr) && ::GetLastError() != ERROR_PIPE_CONNECTED)
			{
				throw ne::Exception("[MessageQueue/Listen]", std::format("Failed to ConnectNamedPipe function (error: {})", ::GetLastError()));
			}
		}

		void_t Connect()
		{
			while (!::WaitNamedPipeW(pipeName.c_str(), NMPWAIT_WAIT_FOREVER))
			{
				if (const auto error = ::GetLastError(); error != ERROR_FILE_NOT_FOUND)
				{
					throw ne::Exception("[MessageQueue/Connect]", std::format("Failed to WaitNamedPipeW function (error: {})", error));
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			handle = ::CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				throw ne::Exception("[MessageQueue/Connect]", std::format("Failed to CreateFileW function (error: {})", ::GetLastError()));
			}
		}

		void_t Send(const std::span<const std::byte> _message) const
		{
			auto bytesWritten = DWORD{};
			if (!::WriteFile(handle, _message.data(), static_cast<DWORD>(_message.size()), &bytesWritten, nullptr))
			{
				throw ne::Exception("[MessageQueue/Send]", std::format("Failed to WriteFile function (error: {})", ::GetLastError()));
			}
		}

		[[nodiscard]] std::vector<std::byte> Receive() const
		{
			auto buffer = std::vector<std::byte>(MaxMessageSize);

			auto bytesRead = DWORD{};
			if (!::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr))
			{
				throw ne::Exception("[MessageQueue/Receive]", std::format("Failed to ReadFile function (error: {})", ::GetLastError()));
			}

			buffer.resize(bytesRead);
			return buffer;
		}

	private:
		static constexpr DWORD MaxMessageSize = 65536;
	};
#elif defined(IS_POSIX)
	class MessageQueue::Impl final
	{
	public:
		explicit Impl(const string_view_t _name)
			: name("/" + string_t(_name))
		{
		}
		~Impl()
		{
			if (handle != InvalidHandle) ::mq_close(handle);
		}

	private:
		static constexpr mqd_t InvalidHandle = static_cast<mqd_t>(-1);

		string_t name;
		mqd_t handle = InvalidHandle;

	public:
		void_t Listen()
		{
			Open();
		}

		void_t Connect()
		{
			Open();
		}

		void_t Send(const std::span<const std::byte> _message) const
		{
			if (::mq_send(handle, reinterpret_cast<const char_t*>(_message.data()), _message.size(), 0) == -1)
			{
				throw ne::Exception("[MessageQueue/Send]", std::format("Failed to mq_send function (error: {})", errno));
			}
		}

		[[nodiscard]] std::vector<std::byte> Receive() const
		{
			auto attributes = mq_attr{};
			if (::mq_getattr(handle, &attributes) == -1)
			{
				throw ne::Exception("[MessageQueue/Receive]", std::format("Failed to mq_getattr function (error: {})", errno));
			}

			auto buffer = std::vector<std::byte>(attributes.mq_msgsize);
			const auto received = ::mq_receive(handle, reinterpret_cast<char_t*>(buffer.data()), buffer.size(), nullptr);
			if (received == -1)
			{
				throw ne::Exception("[MessageQueue/Receive]", std::format("Failed to mq_receive function (error: {})", errno));
			}

			buffer.resize(received);
			return buffer;
		}

	private:
		void_t Open()
		{
			if (handle != InvalidHandle) return;

			handle = ::mq_open(name.c_str(), O_CREAT | O_RDWR, 0666, nullptr);
			if (handle == InvalidHandle)
			{
				throw ne::Exception("[MessageQueue/Open]", std::format("Failed to mq_open function (error: {})", errno));
			}
		}
	};
#endif



	MessageQueue::MessageQueue(const string_view_t _name)
		: impl(std::make_unique<Impl>(_name))
	{
	}
	MessageQueue::~MessageQueue() = default;

	MessageQueue::MessageQueue(MessageQueue&&) noexcept = default;
	MessageQueue& MessageQueue::operator=(MessageQueue&&) noexcept = default;



	void_t MessageQueue::Listen()
	{
		impl->Listen();
	}

	void_t MessageQueue::Connect()
	{
		impl->Connect();
	}

	void_t MessageQueue::Send(const std::span<const std::byte> _message) const
	{
		impl->Send(_message);
	}

	std::vector<std::byte> MessageQueue::Receive() const
	{
		return impl->Receive();
	}

END_NS
