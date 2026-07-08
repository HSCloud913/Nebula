//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <coroutine>
#include <optional>
#include <span>
#include <vector>
#include <cstddef>
#include "Engine/IIoEngine.h"
#include "IoType.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

#if defined(IS_POSIX)
#   include "Engine/IoUring/IoUringEngine.h"
#elif defined(_WIN32)
#   include "Engine/Iocp/IocpEngine.h"
#endif

BEGIN_NS(ne::io)
	// Proactor 완료가 도착하기 전에 코루틴 프레임이 파괴돼도 안전하도록 ProactorContext 를 heap 에
	// 두고 소유권을 엔진과 교대한다(단일 스레드 이벤트 루프 전제). 모든 Submit awaitable 이
	// 멤버로 보유한다 — Reactor 의 WatchEntry(엔진 소유)와 대칭.
	//   정상 완료 : 엔진이 isCompleted 세팅 + resume → await_resume 이 소비 → holder 소멸자가 delete.
	//   중도 폐기 : 완료 전 holder 소멸(프레임 파괴) → isAbandoned 만 세팅, 엔진이 완료 시 delete.
	//   제출 실패 : 커널 참조 없음(failed) → holder 소멸자가 delete.
	// NOTE: 지금은 op 당 new/delete — free-list 풀링은 후속 최적화.
	class ProactorContextHolder
	{
	public:
		ProactorContextHolder() = default;
		~ProactorContextHolder()
		{
			if (!context) return;
			// 커널에 제출됐고(제출 실패 아님) 아직 완료 전이면 소유권을 엔진에 넘긴다.
			if (!failed && !context->isCompleted) context->isAbandoned = true;
			else delete context;
		}

		NEBULA_NON_COPYABLE_MOVABLE(ProactorContextHolder)

		// await_suspend 시작 시 호출 — heap ProactorContext 를 만들고 재개 핸들을 심는다.
		[[nodiscard]] ProactorContext* Prepare(const std::coroutine_handle<> _handle)
		{
			context = new ProactorContext{};
			context->handle = _handle;
			return context;
		}

		// 동기 제출 실패 경로 — 커널 참조가 없으므로 holder 가 직접 해제한다.
		void SetError(ne::OsError _error) noexcept
		{
			failed = true;
			context->result = ne::Result<std::size_t, ne::OsError>::Error(std::move(_error));
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> TakeResult() noexcept { return std::move(context->result); }

	private:
		ProactorContext* context{ nullptr };
		bool_t failed{ false };
	};

	// ── 소켓 이벤트 Awaitable ─────────────────────────────────────────────────

	// 소켓 쓰기 준비 대기.
	// co_await → Result<uint32_t, OsError> : 발생한 IoEvent 플래그
	class SendAwaitable
	{
	public:
		SendAwaitable(const socket_t _fd, IIoEngine& _engine) noexcept
			: fd(_fd)
			, engine(_engine) {}

	private:
		socket_t fd;
		IIoEngine& engine;
		uint32_t triggeredEvents{};
		std::optional<ne::OsError> watchError;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			auto result = engine.Watch(fd, IoEvent::Write | IoEvent::Error,
									[this, _handle](socket_t _triggeredFd, uint32_t _events) mutable
									{
										(void)engine.Unwatch(_triggeredFd, IoEvent::Write); // Read 방향(동시 ReceiveAwaitable)에는 영향 없음
										triggeredEvents = _events;
										_handle.resume();
									});
			if (result.IsError())
			{
				watchError.emplace(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<uint32_t, ne::OsError> await_resume() noexcept
		{
			if (watchError)
				return ne::Result<uint32_t, ne::OsError>::Error(std::move(*watchError));

			return ne::Result<uint32_t, ne::OsError>::Ok(triggeredEvents);
		}
	};

	// 소켓 읽기 준비 대기.
	// co_await → Result<uint32_t, OsError> : 발생한 IoEvent 플래그
	class ReceiveAwaitable
	{
	public:
		ReceiveAwaitable(const socket_t _fd, IIoEngine& _engine) noexcept
			: fd(_fd)
			, engine(_engine) {}

	private:
		socket_t fd;
		IIoEngine& engine;
		uint32_t triggeredEvents{};
		std::optional<ne::OsError> watchError;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			auto result = engine.Watch(fd, IoEvent::Read | IoEvent::HangUp | IoEvent::Error,
									[this, _handle](socket_t _triggeredFd, uint32_t _events) mutable
									{
										(void)engine.Unwatch(_triggeredFd, IoEvent::Read); // Write 방향(동시 SendAwaitable)에는 영향 없음
										triggeredEvents = _events;
										_handle.resume();
									});
			if (result.IsError())
			{
				watchError.emplace(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<uint32_t, ne::OsError> await_resume() noexcept
		{
			if (watchError) return ne::Result<uint32_t, ne::OsError>::Error(std::move(*watchError));
			return ne::Result<uint32_t, ne::OsError>::Ok(triggeredEvents);
		}
	};

	// ── 소켓 Proactor Awaitable ──────────────────────────────────────────────
	// IIoEngine::SubmitSend/SubmitReceive 사용 — 플랫폼 독립(Epoll/IoUring/Iocp 모두
	// socket_t 로 구현). Network/Ipc 양쪽이 동일하게 필요로 해서 여기 공용으로 둔다 —
	// 각자 도메인 어휘(recv/send 이름 외엔 socket_t/IIoEngine 밖에 안 씀)가 없는 순수
	// 엔진 래퍼라 모듈별로 복제해 둘 이유가 없다.

	// 소켓 proactor 송신 대기.
	// co_await → Result<size_t, OsError>
	class SendSubmitAwaitable
	{
	public:
		// _owner: 전송 버퍼가 속한 BufferBlock — 완료 전 풀 반납을 막기 위해 op 가 ref 를 잡는다.
		//         owner 없는(raw) 버퍼면 nullptr(호출자 책임).
		SendSubmitAwaitable(IIoEngine& _engine, const socket_t _fd, const void* _buffer, const std::size_t _length, BufferBlock* _owner = nullptr) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, length(_length)
			, owner(_owner) {}

	private:
		IIoEngine& engine;
		socket_t fd;
		const void* buffer;
		std::size_t length;
		BufferBlock* owner;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ProactorContext* context = holder.Prepare(_handle);
			context->bufferOwner = owner; // IoContext 수명 동안 데이터 버퍼를 살려둔다
			if (owner) owner->AddRef();

			if (auto result = engine.SubmitSend(fd, buffer, length, context); result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

	// 소켓 proactor 수신 대기.
	// co_await → Result<size_t, OsError>
	class ReceiveSubmitAwaitable
	{
	public:
		// _owner: 수신 버퍼가 속한 BufferBlock — 완료 전 풀 반납을 막기 위해 op 가 ref 를 잡는다.
		//         owner 없는(raw) 버퍼면 nullptr(호출자 책임).
		ReceiveSubmitAwaitable(IIoEngine& _engine, const socket_t _fd, void* _buffer, const std::size_t _length, BufferBlock* _owner = nullptr) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, length(_length)
			, owner(_owner) {}

	private:
		IIoEngine& engine;
		socket_t fd;
		void* buffer;
		std::size_t length;
		BufferBlock* owner;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ProactorContext* context = holder.Prepare(_handle);
			context->bufferOwner = owner; // IoContext 수명 동안 데이터 버퍼를 살려둔다
			if (owner) owner->AddRef();

			if (auto result = engine.SubmitReceive(fd, buffer, length, context); result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

	// ── 등록 버퍼 Proactor Awaitable (RegisteredIo) ───────────────────────────
	// IRegisteredBufferProvider 를 통해 제출 — RIO(Win)/registered buffers(io_uring) 공통.
	// 완료 통지는 엔진 이벤트 루프가 IoContext 로 resume(RIO: DrainRioCompletions). owner(BufferBlock)
	// refcount 로 완료 전 버퍼 반납을 막는다(일반 proactor awaitable 과 동일).

	// 등록 버퍼 송신 대기. co_await → Result<size_t, OsError>
	class SendRegisteredSubmitAwaitable
	{
	public:
		SendRegisteredSubmitAwaitable(IRegisteredBufferProvider& _provider, const socket_t _fd, const RegisteredBuffer& _buffer) noexcept
			: provider(_provider)
			, fd(_fd)
			, buffer(_buffer) {}

	private:
		IRegisteredBufferProvider& provider;
		socket_t fd;
		RegisteredBuffer buffer;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ProactorContext* context = holder.Prepare(_handle);
			context->bufferOwner = buffer.view.owner;
			if (buffer.view.owner) buffer.view.owner->AddRef();

			if (auto result = provider.SubmitSendRegistered(fd, buffer, context); result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

	// 등록 버퍼 수신 대기. co_await → Result<size_t, OsError>
	class ReceiveRegisteredSubmitAwaitable
	{
	public:
		ReceiveRegisteredSubmitAwaitable(IRegisteredBufferProvider& _provider, const socket_t _fd, const RegisteredBuffer& _buffer) noexcept
			: provider(_provider)
			, fd(_fd)
			, buffer(_buffer) {}

	private:
		IRegisteredBufferProvider& provider;
		socket_t fd;
		RegisteredBuffer buffer;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ProactorContext* context = holder.Prepare(_handle);
			context->bufferOwner = buffer.view.owner;
			if (buffer.view.owner) buffer.view.owner->AddRef();

			if (auto result = provider.SubmitReceiveRegistered(fd, buffer, context); result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

	// ── 파일 I/O Awaitable ────────────────────────────────────────────────────
	// SubmitRead/SubmitWrite 는 IIoEngine 외부 메서드이므로 구체 엔진 타입을 참조.
	// context 타입은 IoContext 로 통일 (Windows: OVERLAPPED 포함, Linux: 미포함).

#if defined(_WIN32)
	// 파일 읽기 비동기 대기 (IOCP).
	// co_await → Result<size_t, OsError>
	class ReadSubmitAwaitable
	{
	public:
		ReadSubmitAwaitable(IocpEngine& _engine, const file_t _fd, const std::span<ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IocpEngine& engine;
		file_t fd;
		std::span<ne::byte_t> buffer;
		std::size_t offset;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ProactorContext* context = holder.Prepare(_handle);

			auto result = engine.SubmitRead(fd, buffer.data(), buffer.size(), offset, context);
			if (result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

	// 파일 쓰기 비동기 대기 (IOCP).
	// co_await → Result<size_t, OsError>
	class WriteSubmitAwaitable
	{
	public:
		WriteSubmitAwaitable(IocpEngine& _engine, const file_t _fd, const std::span<const ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IocpEngine& engine;
		file_t fd;
		std::span<const ne::byte_t> buffer;
		std::size_t offset;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ProactorContext* context = holder.Prepare(_handle);

			if (auto result = engine.SubmitWrite(fd, buffer.data(), buffer.size(), offset, context); result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

	// 벡터 송신 대기 (IOCP, 단일 overlapped WSASend 로 scatter-gather). WSABUF 배열을 멤버로 보관해
	// 완료 전까지 그 수명을 보장한다(가리키는 데이터 버퍼는 호출자가 co_await 동안 살려둔다).
	// co_await → Result<size_t, OsError>
	class SendvSubmitAwaitable
	{
	public:
		SendvSubmitAwaitable(IocpEngine& _engine, const socket_t _fd, std::vector<WSABUF> _buffers) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffers(std::move(_buffers)) {}

	private:
		IocpEngine& engine;
		socket_t fd;
		std::vector<WSABUF> buffers;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ProactorContext* context = holder.Prepare(_handle);

			if (auto result = engine.SubmitSendv(fd, buffers.data(), static_cast<ne::ulong_t>(buffers.size()), context); result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

	// Proactor + zero-copy 파일 전송 대기 (IOCP TransmitFile). _head/_tail 은 각각 연속된 버퍼
	// 1개만 지원(TRANSMIT_FILE_BUFFERS 제약) — 없으면 기본값 {} 로 생략.
	// co_await → Result<size_t, OsError>
	class TransmitFileSubmitAwaitable
	{
	public:
		TransmitFileSubmitAwaitable(IocpEngine& _engine, const socket_t _socket, const file_t _file, const std::size_t _offset, const std::size_t _size,
			const std::span<ne::byte_t> _head = {}, const std::span<ne::byte_t> _tail = {}) noexcept
			: engine(_engine)
			, socket(_socket)
			, file(_file)
			, offset(_offset)
			, size(_size)
			, head(_head)
			, tail(_tail) {}

	private:
		IocpEngine& engine;
		socket_t socket;
		file_t file;
		std::size_t offset;
		std::size_t size;
		std::span<ne::byte_t> head;
		std::span<ne::byte_t> tail;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ProactorContext* context = holder.Prepare(_handle);

			auto result = engine.SubmitTransmitFile(socket, file, offset, size,
				head.data(), head.size(), tail.data(), tail.size(), context);
			if (result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

#elif defined(IS_POSIX)
	// 파일 읽기 비동기 대기 (io_uring).
	// co_await → Result<size_t, OsError>
	class ReadSubmitAwaitable
	{
	public:
		ReadSubmitAwaitable(IoUringEngine& _engine, const file_t _fd, const std::span<ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IoUringEngine& engine;
		file_t fd;
		std::span<ne::byte_t> buffer;
		std::size_t offset;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			IoContext* context = holder.Prepare(_handle);

			if (auto result = engine.SubmitRead(fd, buffer.data(), buffer.size(), offset, context); result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

	// 파일 쓰기 비동기 대기 (io_uring).
	// co_await → Result<size_t, OsError>
	class WriteSubmitAwaitable
	{
	public:
		WriteSubmitAwaitable(IoUringEngine& _engine, const file_t _fd, const std::span<const ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IoUringEngine& engine;
		file_t fd;
		std::span<const ne::byte_t> buffer;
		std::size_t offset;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			IoContext* context = holder.Prepare(_handle);

			if (auto result = engine.SubmitWrite(fd, buffer.data(), buffer.size(), offset, context); result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

	// 벡터 읽기 비동기 대기 (io_uring readv — 단일 syscall scatter I/O).
	// _iov 가 가리키는 메모리는 완료될 때까지 호출자(AsyncFile::ReadV)가 살려둔다.
	// co_await → Result<size_t, OsError>
	class ReadvSubmitAwaitable
	{
	public:
		ReadvSubmitAwaitable(IoUringEngine& _engine, const file_t _fd, const iovec* _iov, const unsigned _iovcnt, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, iov(_iov)
			, iovcnt(_iovcnt)
			, offset(_offset) {}

	private:
		IoUringEngine& engine;
		file_t fd;
		const iovec* iov;
		unsigned iovcnt;
		std::size_t offset;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			IoContext* context = holder.Prepare(_handle);

			if (auto result = engine.SubmitReadv(fd, iov, iovcnt, offset, context); result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

	// 벡터 쓰기 비동기 대기 (io_uring writev — 단일 syscall gather I/O).
	// co_await → Result<size_t, OsError>
	class WritevSubmitAwaitable
	{
	public:
		WritevSubmitAwaitable(IoUringEngine& _engine, const file_t _fd, const iovec* _iov, const unsigned _iovcnt, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, iov(_iov)
			, iovcnt(_iovcnt)
			, offset(_offset) {}

	private:
		IoUringEngine& engine;
		file_t fd;
		const iovec* iov;
		unsigned iovcnt;
		std::size_t offset;
		ProactorContextHolder holder{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			IoContext* context = holder.Prepare(_handle);

			if (auto result = engine.SubmitWritev(fd, iov, iovcnt, offset, context); result.IsError())
			{
				holder.SetError(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return holder.TakeResult(); }
	};

#endif // platform

END_NS
