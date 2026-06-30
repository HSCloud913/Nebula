//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <span>
#include "Coroutine/Task.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)
	class IIoFile
	{
	public:
		IIoFile() = default;
		virtual ~IIoFile() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IIoFile)

	public:
		[[nodiscard]] virtual ne::Task<ne::Result<std::size_t, ne::OsError>>
			Read(std::span<ne::byte_t> _buf, std::size_t _offset) = 0;

		[[nodiscard]] virtual ne::Task<ne::Result<std::size_t, ne::OsError>>
			Write(std::span<const ne::byte_t> _data, std::size_t _offset) = 0;

		virtual ne::Result<void, ne::OsError> Close() = 0;

	public:
		[[nodiscard]] virtual bool_t IsOpen() const noexcept = 0;
	};
END_NS
