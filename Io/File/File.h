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

BEGIN_NS(ne::io)
	class Context;

	enum class OpenMode : uint_t
	{
		READ,
		WRITE,
		READ_WRITE,
	};

	/**
	 * @class File
	 * @brief 코루틴 기반 비동기 파일 핸들.
	 *
	 * Read/Write/Readv/Writev 는 co_await 지점에서 suspend 되고, 완료 시 Context 의 루프가
	 * 코루틴을 재개한다. OS 파일 핸들을 소유하는 move-only 리소스이며, 사용자가 제공한 버퍼
	 * (span/BufferChain)를 그대로 커널에 넘기므로 완료될 때까지 호출자가 File 과 버퍼의
	 * 수명을 보장해야 한다.
	 */
	class File
	{
	private:
#if defined(_WIN32)
		using FileHandle = ne::Handle<file_t, decltype([](const file_t _handle) { ::CloseHandle(_handle); })>;
#elif defined(IS_POSIX)
		using FileHandle = ne::Handle<file_t, decltype([](const file_t _handle) { ::close(_handle); }), -1>;
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

		[[nodiscard]] ne::Result<void_t, IoError> Close();

	public:
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Read(std::span<ne::byte_t> _buffer, ulonglong_t _offset, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Write(std::span<const ne::byte_t> _buffer, ulonglong_t _offset, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> Readv(const BufferChain& _chain, ulonglong_t _offset, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Writev(const BufferChain& _chain, ulonglong_t _offset, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] file_t Handle() const noexcept { return handle.Get(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};

END_NS
