//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <span>
#include "IIoFile.h"
#include "Handle.h"
#include "Type.h"
#include "IoType.h"

#if defined(IS_POSIX)
#   include "Engine/IoUring/IoUringEngine.h"
#elif defined(_WIN32)
#   include "Engine/Iocp/IocpEngine.h"
#endif

BEGIN_NS(ne::io)
	// io_uring(Linux) / IOCP(Windows) 기반 진짜 비동기 파일 I/O.
	// Read / Write 는 co_await 에서 진짜로 suspend 되며, 완료 시 엔진 스레드가 재개한다.
	class AsyncFile final : public IIoFile
	{
	private:
#if defined(IS_POSIX)
		explicit AsyncFile(file_t _fd, IoUringEngine& _engine) noexcept;
#elif defined(_WIN32)
		explicit AsyncFile(file_t _fd, IocpEngine& _engine) noexcept;
#else
		explicit AsyncFile(file_t _fd) noexcept;
#endif

	public:
		AsyncFile(AsyncFile&& _other) noexcept;
		AsyncFile& operator=(AsyncFile&& _other) noexcept;
		~AsyncFile() override;

    	NEBULA_NON_COPYABLE(AsyncFile)

	private:
		file_t fd;

#if defined(IS_POSIX)
		IoUringEngine* engine{};
#elif defined(_WIN32)
		IocpEngine* engine{};
#endif

	public:
#if defined(IS_POSIX)
		[[nodiscard]] static ne::Result<AsyncFile, ne::OsError>
			Create(const ne::string_t& _path, IoUringEngine& _engine) noexcept;
		[[nodiscard]] static ne::Result<AsyncFile, ne::OsError>
			Open(const ne::string_t& _path, IoUringEngine& _engine, bool_t _readOnly = true) noexcept;
#elif defined(_WIN32)
		[[nodiscard]] static ne::Result<AsyncFile, ne::OsError>
			Create(const ne::string_t& _path, IocpEngine& _engine) noexcept;
		[[nodiscard]] static ne::Result<AsyncFile, ne::OsError>
			Open(const ne::string_t& _path, IocpEngine& _engine, bool_t _readOnly = true) noexcept;
#else
		[[nodiscard]] static ne::Result<AsyncFile, ne::OsError>
			Create(const ne::string_t& _path) noexcept;
		[[nodiscard]] static ne::Result<AsyncFile, ne::OsError>
			Open(const ne::string_t& _path, bool_t _readOnly = true) noexcept;
#endif

	public:
		[[nodiscard]] virtual ne::Task<ne::Result<std::size_t, ne::OsError>>
			Read(std::span<ne::byte_t> _buf, std::size_t _offset) override;

		[[nodiscard]] virtual ne::Task<ne::Result<std::size_t, ne::OsError>>
			Write(std::span<const ne::byte_t> _data, std::size_t _offset) override;

		virtual ne::Result<void, ne::OsError> Close() override;

    public:
		[[nodiscard]] virtual bool_t IsOpen() const noexcept override { return fd != InvalidFile; }
	};
END_NS
