//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <span>
#include "IIoFile.h"
#include "NebulaHandle.h"
#include "Type.h"
#include "IoType.h"

BEGIN_NS(ne::io)
	// 동기 파일 I/O를 코루틴 인터페이스로 래핑.
	// IIoFile 구현체. Linux io_uring / Windows IOCP 연동은 향후 확장.
	class AsyncFile final : public IIoFile
	{
	private:
		explicit AsyncFile(file_t _fd) noexcept;

	public:
		AsyncFile(AsyncFile&& _other) noexcept;
		AsyncFile& operator=(AsyncFile&& _other) noexcept;
		~AsyncFile() override;

    	NEBULA_NON_COPYABLE(AsyncFile)

	private:
		file_t fd;

	public:
    	[[nodiscard]] static ne::Result<AsyncFile, ne::OsError>
			Create(const ne::string_t& _path) noexcept;
    	[[nodiscard]] static ne::Result<AsyncFile, ne::OsError>
			Open(const ne::string_t& _path, bool_t _readOnly = true) noexcept;

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
