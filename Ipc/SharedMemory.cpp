//
// Created by nebula on 24. 5. 29.
//

#include "SharedMemory.h"

#include "Exception.h"
#include "StringFormat.h"

#if defined(_WIN32)
#	include <windows.h>
#elif defined(IS_POSIX)
#	include <sys/mman.h>
#	include <fcntl.h>
#	include <unistd.h>
#	include <cerrno>
#endif



BEGIN_NS(ne::ipc)
#if defined(_WIN32)
	class SharedMemory::Impl final
	{
	public:
		Impl(const string_view_t _name, const std::size_t _size)
			: size(_size)
		{
			const auto wideName = StringFormat::UTF8toWCS(string_t(_name).c_str());

			mappingHandle = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<ulonglong_t>(_size) >> 32, static_cast<ulonglong_t>(_size) & 0xFFFFFFFFu, wideName.c_str());
			if (!mappingHandle)
			{
				throw ne::Exception("[SharedMemory/Impl]", std::format("Failed to CreateFileMappingW function (error: {})", ::GetLastError()));
			}

			view = ::MapViewOfFile(mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, _size);
			if (!view)
			{
				const auto error = ::GetLastError();
				::CloseHandle(mappingHandle);

				throw ne::Exception("[SharedMemory/Impl]", std::format("Failed to MapViewOfFile function (error: {})", error));
			}
		}
		~Impl()
		{
			if (view) ::UnmapViewOfFile(view);
			if (mappingHandle) ::CloseHandle(mappingHandle);
		}

	private:
		std::size_t size;
		HANDLE mappingHandle = nullptr;
		void_t* view = nullptr;

	public:
		[[nodiscard]] std::span<std::byte> GetView() const noexcept { return std::span(static_cast<std::byte*>(view), size); }
	};

#elif defined(IS_POSIX)
	class SharedMemory::Impl final
	{
	public:
		Impl(const string_view_t _name, const std::size_t _size)
			: name("/" + string_t(_name))
			, size(_size)
		{
			handle = ::shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
			if (handle == -1)
			{
				throw ne::Exception("[SharedMemory/Impl]", std::format("Failed to shm_open function (error: {})", errno));
			}

			if (::ftruncate(handle, static_cast<off_t>(_size)) == -1)
			{
				const auto error = errno;
				::close(handle);
				throw ne::Exception("[SharedMemory/Impl]", std::format("Failed to ftruncate function (error: {})", error));
			}

			view = ::mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
			if (view == MAP_FAILED)
			{
				const auto error = errno;
				::close(handle);
				throw ne::Exception("[SharedMemory/Impl]", std::format("Failed to mmap function (error: {})", error));
			}
		}
		~Impl()
		{
			if (view != MAP_FAILED) ::munmap(view, size);
			if (handle != -1) ::close(handle);
			::shm_unlink(name.c_str());
		}

	private:
		string_t name;
		std::size_t size;
		int_t handle = -1;
		void_t* view = MAP_FAILED;

	public:
		[[nodiscard]] std::span<std::byte> GetView() const noexcept { return std::span(static_cast<std::byte*>(view), size); }
	};
#endif



	SharedMemory::SharedMemory(const string_view_t _name, const std::size_t _size)
		: impl(std::make_unique<Impl>(_name, _size)) {}
	SharedMemory::~SharedMemory() = default;

	SharedMemory::SharedMemory(SharedMemory&&) noexcept = default;
	SharedMemory& SharedMemory::operator=(SharedMemory&&) noexcept = default;



	std::span<std::byte> SharedMemory::GetView() const noexcept
	{
		return impl->GetView();
	}

END_NS
