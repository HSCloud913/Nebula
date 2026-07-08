//
// Created by hscloud on 26. 7. 8.
//
// Level 3 — 코루틴 기반 비동기 파일. Read/Write 는 co_await 에서 suspend 되고 완료 시 IoContext
// 루프가 재개한다. 값 기반(규칙 4): OS 핸들을 소유하는 move-only 리소스. 버퍼는 사용자가 제공한
// span 을 그대로 커널에 넘기며, 완료될 때까지 호출자가 File 과 버퍼의 lifetime 을 보장한다.

#pragma once
#include <cstddef>
#include <span>
#include "Type.h"
#include "IoType.h"
#include "Handle.h"
#include "IoResult.h"
#include "Coroutine/Task.h"

BEGIN_NS(ne::io)
	class IoContext;

	enum class OpenMode : uint_t
	{
		Read,      // 기존 파일 읽기 전용 (OPEN_EXISTING / O_RDONLY)
		Write,     // 생성·트렁케이트 후 쓰기 (CREATE_ALWAYS / O_WRONLY|O_CREAT|O_TRUNC)
		ReadWrite, // 없으면 생성, 읽기+쓰기 (OPEN_ALWAYS / O_RDWR|O_CREAT)
	};

	namespace detail
	{
		// 플랫폼 close 를 .cpp 로 숨겨 헤더가 OS 헤더에 의존하지 않게 한다(파일 핸들 deleter).
		void_t CloseFileHandle(file_t _handle) noexcept;

		struct FileHandleDeleter
		{
			void_t operator()(const file_t _handle) const noexcept { CloseFileHandle(_handle); }
		};
	}

	class File
	{
	public:
		NEBULA_NON_COPYABLE(File)
		NEBULA_DEFAULT_MOVE(File)

		~File() = default;

	private:
#if defined(_WIN32)
		using FileHandle = ne::Handle<file_t, detail::FileHandleDeleter>;              // 무효값 = nullptr(기본)
#elif defined(IS_POSIX)
		using FileHandle = ne::Handle<file_t, detail::FileHandleDeleter, -1>;          // 무효값 = -1
#endif

		File(FileHandle&& _handle, IoContext& _context) noexcept;

	private:
		FileHandle  handle;
		IoContext*  context;

	public:
		[[nodiscard]] static IoResult<File> Open(IoContext& _context, string_view_t _path, OpenMode _mode);

		[[nodiscard]] ne::Task<IoResult<std::size_t>> Read(std::span<ne::byte_t> _buffer, ulonglong_t _offset);
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Write(std::span<const ne::byte_t> _buffer, ulonglong_t _offset);

		// 명시적 해제(silently close 하는 소멸자와 별도, 규칙). 이후 IsValid()==false.
		[[nodiscard]] ne::Result<void_t, IoError> Close();

	public:
		[[nodiscard]] file_t Handle() const noexcept { return handle.Get(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};

END_NS
