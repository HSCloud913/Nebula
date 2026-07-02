//
// Created by nebula on 24. 5. 29.
//

#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <memory>

#include "Type.h"

BEGIN_NS(ne::ipc)
	class Semaphore final
	{
	public:
		Semaphore(string_view_t _name, int_t _initialCount);
		~Semaphore();

		Semaphore(Semaphore&&) noexcept;
		Semaphore& operator=(Semaphore&&) noexcept;

		NEBULA_NON_COPYABLE(Semaphore)

	public:
		void_t Acquire() const;
		[[nodiscard]] bool_t TryAcquire() const;
		void_t Release(int_t _count = 1) const;

	private:
		class Impl;
		std::unique_ptr<Impl> impl;
	};

END_NS

#endif //SEMAPHORE_H
