//
// Created by nebula on 24. 5. 29.
//

#ifndef SHAREDMEMORY_H
#define SHAREDMEMORY_H

#include <memory>
#include <span>

#include "Base/Type.h"

BEGIN_NS(ne::ipc)
	class SharedMemory final
	{
	public:
		SharedMemory(string_view_t _name, std::size_t _size);
		~SharedMemory();

		SharedMemory(SharedMemory&&) noexcept;
		SharedMemory& operator=(SharedMemory&&) noexcept;

		NEBULA_NON_COPYABLE(SharedMemory)

	private:
		class Impl;
		std::unique_ptr<Impl> impl;

	public:
		[[nodiscard]] std::span<std::byte> GetView() const noexcept;
	};

END_NS

#endif //SHAREDMEMORY_H
