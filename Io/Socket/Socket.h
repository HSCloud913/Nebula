//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include <cstddef>
#include <span>
#include <stop_token>
#include "Base/Type.h"
#include "Io/IoType.h"
#include "Base/Handle.h"
#include "Io/IoResult.h"
#include "Base/Coroutine/Task.h"
#include "Io/Buffer/BufferChain.h"
#include "Io/Engine/IRegisteredBufferProvider.h"

BEGIN_NS(ne::io)
	class Context;

	/**
	 * @class Socket
	 * @brief 코루틴 기반 비동기 소켓.
	 *
	 * Receive/Send/Accept/Connect 등은 co_await 지점에서 suspend 되고, 완료 시 Context 의
	 * 루프가 코루틴을 재개한다. OS 소켓 핸들을 소유하는 move-only 리소스이며, RIO(Registered I/O)로
	 * 생성됐는지 여부를 함께 들고 있어 SendZeroCopy 가능 여부를 판별한다. stream/datagram,
	 * scatter-gather, readiness 대기, zero-copy 송신 등 다양한 데이터 경로를 제공한다.
	 */
	class Socket
	{
	private:
#if defined(_WIN32)
		using SocketHandle = ne::Handle<socket_t, decltype([](const socket_t _handle) { ::closesocket(_handle); }), INVALID_SOCKET>;
#elif defined(IS_POSIX)
		using SocketHandle = ne::Handle<socket_t, decltype([](const socket_t _handle) { ::close(_handle); }), -1>;
#endif

	private:
		Socket(SocketHandle&& _handle, Context& _context, bool_t _isRegisteredIo) noexcept;

	public:
		~Socket() = default;

		NEBULA_NON_COPYABLE(Socket)
		NEBULA_DEFAULT_MOVE(Socket)

	private:
		SocketHandle handle;
		Context* context;
		bool_t isRegisteredIo{ false };

	public:
		[[nodiscard]] static IoResult<Socket> Create(Context& _context, int_t _family, int_t _type = SOCK_STREAM, int_t _protocol = IPPROTO_TCP, bool_t _isRegisteredIo = false);

		[[nodiscard]] static IoResult<Socket> Attach(socket_t _handle, Context& _context, bool_t _isRegisteredIo = false);

	public:
		[[nodiscard]] ne::Result<void_t, IoError> Bind(string_view_t _ip, uint16_t _port);

		[[nodiscard]] ne::Result<void_t, IoError> Listen(int_t _backlog = SOMAXCONN);

		[[nodiscard]] ne::Task<IoResult<Socket>> Accept(bool_t _isRegisteredIo = false, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] ne::Task<ne::Result<void_t, IoError>> Connect(string_view_t _ip, uint16_t _port = 0, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receive(std::span<ne::byte_t> _buffer, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Send(std::span<const ne::byte_t> _buffer, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receivev(const BufferChain& _chain, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Sendv(const BufferChain& _chain, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<ne::Result<void_t, IoError>> WaitReadable(std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<ne::Result<void_t, IoError>> WaitWritable(std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendZeroCopy(BufferHandle _handle, std::span<const ne::byte_t> _buffer, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendFile(file_t _file, ulonglong_t _offset, std::size_t _length, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendTo(std::span<const ne::byte_t> _buffer, string_view_t _ip, uint16_t _port = 0, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> ReceiveFrom(std::span<ne::byte_t> _buffer, string_t& _ip, uint16_t& _port, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] ne::Result<void_t, IoError> SetReuseAddress(bool_t _enable);
		[[nodiscard]] ne::Result<void_t, IoError> SetNoDelay(bool_t _enable);

	public:
		[[nodiscard]] ne::Result<void_t, IoError> Shutdown();
		[[nodiscard]] ne::Result<void_t, IoError> Close();

	public:
		[[nodiscard]] socket_t Handle() const noexcept { return handle.Get(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
		[[nodiscard]] bool_t IsRegisteredIo() const noexcept { return isRegisteredIo; }
	};

END_NS
