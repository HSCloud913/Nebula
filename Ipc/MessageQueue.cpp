//
// Created by nebula on 24. 5. 29.
//

#include "MessageQueue.h"

#include "Exception.h"
#include "StringFormat.h"
#include "Engine/IIoEngine.h"
#include "Engine/IoAwaitable.h"
#include <coroutine>
#include <thread>

#if defined(_WIN32)
#	ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#	endif
#	include <winsock2.h>
#	include <windows.h>
#elif defined(IS_POSIX)
#	include <mqueue.h>
#	include <fcntl.h>
#	include <cerrno>
#endif



BEGIN_NS(ne::ipc)

// ─── Impl ───────────────────────────────────────────────────────────────────

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
		[[nodiscard]] HANDLE Handle() const noexcept { return handle; }
		static constexpr DWORD MaxMsg = 65536;

	public:
		void_t Listen()
		{
			handle = ::CreateNamedPipeW(
				pipeName.c_str(),
				PIPE_ACCESS_DUPLEX,
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
				1,
				MaxMsg,
				MaxMsg,
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
				Sleep(1);
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
			auto buffer = std::vector<std::byte>(MaxMsg);

			auto bytesRead = DWORD{};
			if (!::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr))
			{
				throw ne::Exception("[MessageQueue/Receive]", std::format("Failed to ReadFile function (error: {})", ::GetLastError()));
			}

			buffer.resize(bytesRead);
			return buffer;
		}
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
		[[nodiscard]] mqd_t Handle() const noexcept { return handle; }

		[[nodiscard]] long MsgSize() const noexcept
		{
			mq_attr attr{};
			::mq_getattr(handle, &attr);
			return attr.mq_msgsize;
		}

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



// ─── 공개 메서드 ─────────────────────────────────────────────────────────────

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



// ─── 비동기 awaitable ────────────────────────────────────────────────────────

#if defined(IS_POSIX)
// POSIX: mqd_t 는 Linux 에서 진짜 fd → Watch 로 직접 감시

	ne::Task<ne::Result<void, ne::OsError>>
	MessageQueue::SendAsync(const std::span<const std::byte> _message, ne::io::IIoEngine& _engine)
	{
		auto watched = co_await ne::io::WatchAwaitable{
			_engine,
			static_cast<ne::io::io_fd_t>(impl->Handle()),
			ne::io::IoEvent::Write };
		if (watched.IsError())
			co_return ne::Result<void, ne::OsError>::Error(std::move(watched.Error()));
		if (watched.Value() & (ne::io::IoEvent::Error | ne::io::IoEvent::HangUp))
			co_return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ 0, "[MessageQueue/SendAsync] mq error" });

		if (::mq_send(impl->Handle(),
			reinterpret_cast<const char_t*>(_message.data()), _message.size(), 0) == -1)
		{
			co_return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[MessageQueue/SendAsync]"));
		}
		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Task<ne::Result<std::vector<std::byte>, ne::OsError>>
	MessageQueue::ReceiveAsync(ne::io::IIoEngine& _engine)
	{
		auto watched = co_await ne::io::WatchAwaitable{
			_engine,
			static_cast<ne::io::io_fd_t>(impl->Handle()),
			ne::io::IoEvent::Read };
		if (watched.IsError())
			co_return ne::Result<std::vector<std::byte>, ne::OsError>::Error(std::move(watched.Error()));
		if (watched.Value() & (ne::io::IoEvent::Error | ne::io::IoEvent::HangUp))
			co_return ne::Result<std::vector<std::byte>, ne::OsError>::Error(
				ne::OsError{ 0, "[MessageQueue/ReceiveAsync] mq error" });

		// 감시 완료 후 mq_receive 는 즉시 반환
		const auto msgSize = impl->MsgSize();
		auto buffer = std::vector<std::byte>(static_cast<std::size_t>(msgSize));
		const auto received = ::mq_receive(
			impl->Handle(),
			reinterpret_cast<char_t*>(buffer.data()),
			static_cast<std::size_t>(msgSize),
			nullptr);

		if (received < 0)
			co_return ne::Result<std::vector<std::byte>, ne::OsError>::Error(
				ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[MessageQueue/ReceiveAsync]"));

		buffer.resize(static_cast<std::size_t>(received));
		co_return ne::Result<std::vector<std::byte>, ne::OsError>::Ok(std::move(buffer));
	}

#elif defined(_WIN32)
// Windows: 명명 파이프 HANDLE — IocpEngine 은 WSARecv 사용으로 pipe 에 적용 불가
// approach (b): bridge 스레드가 blocking Read/Write 후 coroutine_handle::resume()
// _engine 파라미터는 API 일관성을 위해 수락하나 사용하지 않음

namespace
{
	struct MqSendBridgeAwaitable
	{
		HANDLE pipeHandle;
		const std::byte* data;
		DWORD dataSize;
		DWORD lastError{ 0 };
		bool hasError{ false };
		std::coroutine_handle<> handle{};

		[[nodiscard]] bool await_ready() const noexcept { return false; }

		bool await_suspend(std::coroutine_handle<> _h) noexcept
		{
			handle = _h;
			// C++ 표준: 코루틴은 await_suspend 진입 전에 이미 suspended → 스레드에서 resume() 안전
			std::thread([this]
			{
				DWORD written{};
				if (!::WriteFile(pipeHandle, data, dataSize, &written, nullptr))
					{ hasError = true; lastError = ::GetLastError(); }
				handle.resume();
			}).detach();
			return true;
		}

		[[nodiscard]] ne::Result<void, ne::OsError> await_resume() noexcept
		{
			if (hasError)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ lastError }.Context("[MessageQueue/SendAsync]"));
			return ne::Result<void, ne::OsError>::Ok();
		}
	};

	struct MqReceiveBridgeAwaitable
	{
		HANDLE pipeHandle;
		DWORD maxMsgSize;
		std::vector<std::byte> result;
		DWORD lastError{ 0 };
		bool hasError{ false };
		std::coroutine_handle<> handle{};

		[[nodiscard]] bool await_ready() const noexcept { return false; }

		bool await_suspend(std::coroutine_handle<> _h) noexcept
		{
			handle = _h;
			std::thread([this]
			{
				auto buf = std::vector<std::byte>(maxMsgSize);
				DWORD bytesRead{};
				if (!::ReadFile(pipeHandle, buf.data(), maxMsgSize, &bytesRead, nullptr))
				{
					hasError = true;
					lastError = ::GetLastError();
				}
				else
				{
					buf.resize(bytesRead);
					result = std::move(buf);
				}
				handle.resume();
			}).detach();
			return true;
		}

		[[nodiscard]] ne::Result<std::vector<std::byte>, ne::OsError> await_resume() noexcept
		{
			if (hasError)
				return ne::Result<std::vector<std::byte>, ne::OsError>::Error(
					ne::OsError{ lastError }.Context("[MessageQueue/ReceiveAsync]"));
			return ne::Result<std::vector<std::byte>, ne::OsError>::Ok(std::move(result));
		}
	};
} // anonymous namespace

	ne::Task<ne::Result<void, ne::OsError>>
	MessageQueue::SendAsync(const std::span<const std::byte> _message, ne::io::IIoEngine& /*_engine*/)
	{
		co_return co_await MqSendBridgeAwaitable{
			impl->Handle(),
			_message.data(),
			static_cast<DWORD>(_message.size())
		};
	}

	ne::Task<ne::Result<std::vector<std::byte>, ne::OsError>>
	MessageQueue::ReceiveAsync(ne::io::IIoEngine& /*_engine*/)
	{
		co_return co_await MqReceiveBridgeAwaitable{ impl->Handle(), Impl::MaxMsg };
	}

#else
	ne::Task<ne::Result<void, ne::OsError>>
	MessageQueue::SendAsync(const std::span<const std::byte>, ne::io::IIoEngine&)
	{
		co_return ne::Result<void, ne::OsError>::Error(
			ne::OsError{ 0, "[MessageQueue/SendAsync] not supported on this platform" });
	}

	ne::Task<ne::Result<std::vector<std::byte>, ne::OsError>>
	MessageQueue::ReceiveAsync(ne::io::IIoEngine&)
	{
		co_return ne::Result<std::vector<std::byte>, ne::OsError>::Error(
			ne::OsError{ 0, "[MessageQueue/ReceiveAsync] not supported on this platform" });
	}
#endif

END_NS
