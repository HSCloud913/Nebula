//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include <cstddef>
#include <span>
#include <stop_token>
#include "Base/Type.h"
#include "Diagnostic/Type.h"
#include "Base/Handle.h"
#include "Io/Diagnostic/Error.h"
#include "Base/Coroutine/Task.h"
#include "Memory/Buffer/BufferChain.h"
#include "Io/Buffer/RegisteredBufferProvider.h"

namespace ne::io
{
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
		~Socket();

		NEBULA_NON_COPYABLE(Socket)
		Socket(Socket&&) noexcept = default;
		Socket& operator=(Socket&&) noexcept;

	private:
		SocketHandle handle;
		Context* context;
		bool_t isRegisteredIo{ false };

		// 이 소켓 핸들이 닫히기 직전, 엔진의 IOCP 연관 캐시에서 핸들 값을 제거한다(값 재사용 오연관 방지).
		void_t DeregisterFromEngine() noexcept;

	public:
		[[nodiscard]] static IoResult<Socket> Create(Context& _context, int_t _family, int_t _type = SOCK_STREAM, int_t _protocol = IPPROTO_TCP, bool_t _isRegisteredIo = false);

		[[nodiscard]] static IoResult<Socket> Attach(socket_t _handle, Context& _context, bool_t _isRegisteredIo = false);

	public:
		[[nodiscard]] IoResult<void_t> Bind(string_view_t _ip, uint16_t _port);

		[[nodiscard]] IoResult<void_t> Listen(int_t _backlog = SOMAXCONN);

		[[nodiscard]] ne::Task<IoResult<Socket>> Accept(bool_t _isRegisteredIo = false, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] ne::Task<IoResult<void_t>> Connect(string_view_t _ip, uint16_t _port = 0, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receive(std::span<ne::byte_t> _buffer, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Send(std::span<const ne::byte_t> _buffer, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receivev(const ne::memory::BufferChain& _chain, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Sendv(const ne::memory::BufferChain& _chain, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<void_t>> WaitReadable(std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<void_t>> WaitWritable(std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendZeroCopy(BufferHandle _handle, std::span<const ne::byte_t> _buffer, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendFile(file_t _file, ulonglong_t _offset, std::size_t _length, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendTo(std::span<const ne::byte_t> _buffer, string_view_t _ip, uint16_t _port = 0, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> ReceiveFrom(std::span<ne::byte_t> _buffer, string_t& _ip, uint16_t& _port, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] IoResult<void_t> SetReuseAddress(bool_t _enable);
		[[nodiscard]] IoResult<void_t> SetNoDelay(bool_t _enable);

	public:
		/**
		 * @brief 이 소켓에 바인딩된 로컬 포트를 반환합니다(조회 실패 시 0).
		 *
		 * 포트 0 으로 Bind 해 커널이 임시 포트를 배정한 뒤 그 값을 알아내는 것이 주 용도입니다.
		 */
		[[nodiscard]] uint16_t LocalPort() const noexcept;

	public:
		[[nodiscard]] IoResult<void_t> Shutdown();
		[[nodiscard]] IoResult<void_t> Close();

	public:
		[[nodiscard]] socket_t Handle() const noexcept { return handle.Get(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
		[[nodiscard]] bool_t IsRegisteredIo() const noexcept { return isRegisteredIo; }
	};
}
