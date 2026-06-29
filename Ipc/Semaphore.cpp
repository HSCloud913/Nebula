//
// Created by nebula on 24. 5. 29.
//

#include "Semaphore.h"

#include "Exception.h"
#include "StringFormat.h"

#if defined(_WIN32)
#	include <windows.h>
#elif defined(IS_POSIX)
#	include <semaphore.h>
#	include <fcntl.h>
#	include <cerrno>
#endif

BEGIN_NS(ne::protocol::Ipc)
#if defined(_WIN32)
	class Semaphore::Impl final
	{
	public:
		Impl(const string_view_t _name, const int_t _initialCount)
		{
			const auto wideName = StringFormat::UTF8toWCS(string_t(_name).c_str());

			handle = ::CreateSemaphoreW(nullptr, _initialCount, MaxCount, wideName.c_str());
			if (!handle)
			{
				throw ne::Exception("[Semaphore/Impl]", std::format("Failed to CreateSemaphoreW function (error: {})", ::GetLastError()));
			}
		}
		~Impl()
		{
			if (handle) ::CloseHandle(handle);
		}

	private:
		static constexpr long_t MaxCount = 1'000'000'000;

		HANDLE handle = nullptr;

	public:
		void_t Acquire() const
		{
			if (::WaitForSingleObject(handle, INFINITE) != WAIT_OBJECT_0)
			{
				throw ne::Exception("[Semaphore/Acquire]", std::format("Failed to WaitForSingleObject function (error: {})", ::GetLastError()));
			}
		}

		[[nodiscard]] bool_t TryAcquire() const
		{
			if (const auto result = ::WaitForSingleObject(handle, 0); result == WAIT_OBJECT_0)
			{
				return true;
			}
			else if (result == WAIT_TIMEOUT)
			{
				return false;
			}
			else
			{
				throw ne::Exception("[Semaphore/TryAcquire]", std::format("Failed to WaitForSingleObject function (error: {})", ::GetLastError()));
			}
		}

		void_t Release(const int_t _count) const
		{
			if (!::ReleaseSemaphore(handle, _count, nullptr))
			{
				throw ne::Exception("[Semaphore/Release]", std::format("Failed to ReleaseSemaphore function (error: {})", ::GetLastError()));
			}
		}
	};
#elif defined(IS_POSIX)
	class Semaphore::Impl final
	{
	public:
		Impl(const string_view_t _name, const int_t _initialCount)
			: name("/" + string_t(_name))
		{
			handle = ::sem_open(name.c_str(), O_CREAT, 0666, _initialCount);
			if (handle == SEM_FAILED)
			{
				throw ne::Exception("[Semaphore/Impl]", std::format("Failed to sem_open function (error: {})", errno));
			}
		}
		~Impl()
		{
			if (handle != SEM_FAILED) ::sem_close(handle);
			::sem_unlink(name.c_str());
		}

	private:
		string_t name;
		sem_t* handle = SEM_FAILED;

	public:
		void_t Acquire() const
		{
			while (::sem_wait(handle) == -1)
			{
				if (errno != EINTR)
				{
					throw ne::Exception("[Semaphore/Acquire]", std::format("Failed to sem_wait function (error: {})", errno));
				}
			}
		}

		[[nodiscard]] bool_t TryAcquire() const
		{
			if (::sem_trywait(handle) == 0) return true;
			if (errno == EAGAIN) return false;

			throw ne::Exception("[Semaphore/TryAcquire]", std::format("Failed to sem_trywait function (error: {})", errno));
		}

		void_t Release(const int_t _count) const
		{
			for (auto i = 0; i < _count; ++i)
			{
				if (::sem_post(handle) == -1)
				{
					throw ne::Exception("[Semaphore/Release]", std::format("Failed to sem_post function (error: {})", errno));
				}
			}
		}
	};
#endif



	Semaphore::Semaphore(const string_view_t _name, const int_t _initialCount)
		: impl(std::make_unique<Impl>(_name, _initialCount))
	{
	}
	Semaphore::~Semaphore() = default;

	Semaphore::Semaphore(Semaphore&&) noexcept = default;
	Semaphore& Semaphore::operator=(Semaphore&&) noexcept = default;



	void_t Semaphore::Acquire() const
	{
		impl->Acquire();
	}

	bool_t Semaphore::TryAcquire() const
	{
		return impl->TryAcquire();
	}

	void_t Semaphore::Release(const int_t _count) const
	{
		impl->Release(_count);
	}

END_NS
