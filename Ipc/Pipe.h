//
// Created by nebula on 24. 5. 29.
//

#ifndef PIPE_H
#define PIPE_H

#include <memory>
#include <span>

#include "Type.h"

BEGIN_NS(ne::ipc)
	class Pipe final
	{
	public:
		explicit Pipe(string_view_t _name);
		~Pipe();

		Pipe(Pipe&&) noexcept;
		Pipe& operator=(Pipe&&) noexcept;

		NEBULA_NON_COPYABLE(Pipe)

	public:
		void_t Listen();
		void_t Connect();
		[[nodiscard]] bool_t IsConnected() const noexcept;

	public:
		[[nodiscard]] longlong_t Read(std::span<std::byte> _buffer) const;
		[[nodiscard]] bool_t Write(std::span<const std::byte> _data) const;

	private:
		class Impl;
		std::unique_ptr<Impl> impl;
	};

END_NS

#endif //PIPE_H
