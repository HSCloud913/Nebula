//
// Created by hscloud on 26. 7. 8.
//
// Level 3 — 코루틴 기반 비동기 파일. Read/Write 는 co_await 에서 suspend 되고 완료 시 Context
// 루프가 재개한다. 값 기반(규칙 4): OS 핸들을 소유하는 move-only 리소스. 버퍼는 사용자가 제공한
// span 을 그대로 커널에 넘기며, 완료될 때까지 호출자가 File 과 버퍼의 lifetime 을 보장한다.

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

BEGIN_NS(ne::io)
	class Context;

	enum class OpenMode : uint_t
	{
		Read,
		// 기존 파일 읽기 전용 (OPEN_EXISTING / O_RDONLY)
		Write,
		// 생성·트렁케이트 후 쓰기 (CREATE_ALWAYS / O_WRONLY|O_CREAT|O_TRUNC)
		ReadWrite,
		// 없으면 생성, 읽기+쓰기 (OPEN_ALWAYS / O_RDWR|O_CREAT)
	};

	class File
	{
	private:
#if defined(_WIN32)
		using FileHandle = ne::Handle<file_t, decltype([](const file_t _handle) { ::CloseHandle(_handle); })>;
#elif defined(IS_POSIX)
		using FileHandle = ne::Handle<file_t, decltype([](const file_t _handle) { ::close(_handle); }), -1>; // 무효값 = -1
#endif

	private:
		File(FileHandle&& _handle, Context& _context) noexcept;

	public:
		~File() = default;

		NEBULA_NON_COPYABLE(File)
		NEBULA_DEFAULT_MOVE(File)

	private:
		FileHandle handle;
		Context* context;

	public:
		[[nodiscard]] static IoResult<File> Open(Context& _context, string_view_t _path, OpenMode _mode);

		// 명시적 해제(silently close 하는 소멸자와 별도, 규칙). 이후 IsValid()==false.
		[[nodiscard]] ne::Result<void_t, IoError> Close();

	public:
		// _stopToken: stop 되면 진행 중인 op 를 커널 취소한다(Io::Awaitable 계약 그대로). 기본값(빈
		// 토큰)은 취소 없음 — when_any/Timeout 콤비네이터가 타임아웃 경합에서 채워 넣는다.
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Read(std::span<ne::byte_t> _buffer, ulonglong_t _offset, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Write(std::span<const ne::byte_t> _buffer, ulonglong_t _offset, std::stop_token _stopToken = {});

		// scatter/gather — 여러 세그먼트를 한 요청으로 읽고/쓴다. _chain 은 완료까지 호출자가 살려둬야 한다.
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Readv(const BufferChain& _chain, ulonglong_t _offset, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Writev(const BufferChain& _chain, ulonglong_t _offset, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] file_t Handle() const noexcept { return handle.Get(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};

END_NS
