//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <span>
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)
	// 파일을 메모리 맵으로 열어 span으로 접근.
	class MemoryMapFile
	{
	private:
		MemoryMapFile() noexcept = default;

	public:
		MemoryMapFile(MemoryMapFile&& _other) noexcept;
		MemoryMapFile& operator=(MemoryMapFile&& _other) noexcept;
		~MemoryMapFile() { Close(); }

		NEBULA_NON_COPYABLE(MemoryMapFile)

	private:
		std::size_t size{};
		void* mapping{ nullptr };
#if defined(_WIN32)
		void* fileHandle{ nullptr };
		void* mapHandle{ nullptr };
#endif

	public:
		[[nodiscard]] static ne::Result<MemoryMapFile, ne::OsError> Open(const ne::string_t& _path) noexcept;
		void Close() noexcept;

	public:
		[[nodiscard]] std::span<const ne::byte_t> Data() const noexcept { return { static_cast<const ne::byte_t*>(mapping), size }; }
		[[nodiscard]] std::size_t Size() const noexcept { return size; }
		[[nodiscard]] bool_t IsOpen() const noexcept { return mapping != nullptr; }
	};

END_NS
