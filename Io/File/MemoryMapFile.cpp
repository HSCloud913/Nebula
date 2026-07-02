//
// Created by hscloud on 26. 6. 30.
//

#include "MemoryMapFile.h"
#include <utility>

#if defined(_WIN32)
#   include <windows.h>
#elif defined(IS_POSIX)
#   include <fcntl.h>
#   include <unistd.h>
#   include <sys/mman.h>
#   include <sys/stat.h>
#endif

BEGIN_NS(ne::io)
	MemoryMapFile::MemoryMapFile(MemoryMapFile&& _other) noexcept
		: size(std::exchange(_other.size, 0))
		, mapping(std::exchange(_other.mapping, nullptr))
#if defined(_WIN32)
		, fileHandle(std::exchange(_other.fileHandle, nullptr))
		, mapHandle(std::exchange(_other.mapHandle, nullptr))
#endif
	{}

	MemoryMapFile& MemoryMapFile::operator=(MemoryMapFile&& _other) noexcept
	{
		if (this != &_other)
		{
			Close();
			size    = std::exchange(_other.size, 0);
			mapping = std::exchange(_other.mapping, nullptr);
#if defined(_WIN32)
			fileHandle = std::exchange(_other.fileHandle, nullptr);
			mapHandle  = std::exchange(_other.mapHandle,  nullptr);
#endif
		}
		return *this;
	}



	ne::Result<MemoryMapFile, ne::OsError> MemoryMapFile::Open(const ne::string_t& _path) noexcept
	{
#if defined(_WIN32)
		const HANDLE handle = ::CreateFileA(_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
			return ne::Result<MemoryMapFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[MemoryMapFile/Open]"));

		LARGE_INTEGER size{};
		::GetFileSizeEx(handle, &size);
		const auto fileSize = static_cast<std::size_t>(size.QuadPart);

		const HANDLE memoryHandle = ::CreateFileMappingA(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (!memoryHandle)
		{
			::CloseHandle(handle);

			return ne::Result<MemoryMapFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[MemoryMapFile/CreateMapping]"));
		}

		void* ptr = ::MapViewOfFile(memoryHandle, FILE_MAP_READ, 0, 0, 0);
		if (!ptr)
		{
			::CloseHandle(memoryHandle);
			::CloseHandle(handle);

			return ne::Result<MemoryMapFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[MemoryMapFile/MapView]"));
		}

		MemoryMapFile memoryMapFile;
		memoryMapFile.mapping    = ptr;
		memoryMapFile.size       = fileSize;
		memoryMapFile.fileHandle = handle;
		memoryMapFile.mapHandle  = memoryHandle;

		return ne::Result<MemoryMapFile, ne::OsError>::Ok(std::move(memoryMapFile));

#elif defined(IS_POSIX)
		const int fd = ::open(_path.c_str(), O_RDONLY);
		if (fd < 0)
			return ne::Result<MemoryMapFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[MemoryMapFile/Open]"));

		struct stat st{};
		::fstat(fd, &st);
		const auto fileSize = static_cast<std::size_t>(st.st_size);

		void* ptr = ::mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
		::close(fd);

		if (ptr == MAP_FAILED)
			return ne::Result<MemoryMapFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[MemoryMapFile/mmap]"));

		MemoryMapFile memoryMapFile;
		memoryMapFile.mapping = ptr;
		memoryMapFile.size    = fileSize;

		return ne::Result<MemoryMapFile, ne::OsError>::Ok(std::move(memoryMapFile));

#else
		(void)_path;

		return ne::Result<MemoryMapFile, ne::OsError>::Error(
			ne::OsError{ 0, "MemoryMapFile not supported on this platform" });
#endif
	}

	void MemoryMapFile::Close() noexcept
	{
		if (!mapping) return;

#if defined(_WIN32)
		::UnmapViewOfFile(mapping);
		if (mapHandle)  ::CloseHandle(static_cast<HANDLE>(mapHandle));
		if (fileHandle) ::CloseHandle(static_cast<HANDLE>(fileHandle));
		mapping = mapHandle = fileHandle = nullptr;
#elif defined(IS_POSIX)
		::munmap(mapping, size);
		mapping = nullptr;
#endif
		size = 0;
	}

END_NS
