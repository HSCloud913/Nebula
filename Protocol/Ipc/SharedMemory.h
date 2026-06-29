//
// Created by nebula on 24. 5. 29.
//

#ifndef SHAREDMEMORY_H
#define SHAREDMEMORY_H

#include <memory>
#include <span>

#include "Type.h"

BEGIN_NS(ne::protocol::Ipc)
	class SharedMemory final
	{
		NEBULA_NON_COPYABLE(SharedMemory)

	public:
		SharedMemory(string_view_t _name, std::size_t _size);
		~SharedMemory();

		SharedMemory(SharedMemory&&) noexcept;
		SharedMemory& operator=(SharedMemory&&) noexcept;

	public:
		[[nodiscard]] std::span<std::byte> GetView() const noexcept;

	private:
		class Impl;
		std::unique_ptr<Impl> impl;
	};

END_NS

typedef ne::protocol::Ipc::SharedMemory NebulaSharedMemory;

#endif //SHAREDMEMORY_H
