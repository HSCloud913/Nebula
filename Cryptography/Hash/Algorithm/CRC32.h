#pragma once
#include "Type.h"

BEGIN_NS(ne::crypto)
	class CRC32 final
	{
	public:
		explicit CRC32() = default;
		~CRC32() = default;

	private:
		uint_t hash = 0;

	public:
		void Init() { hash = 0; }
		void AddBuffer(const void_t* _data, size_t _dataLength);
		[[nodiscard]] string_t Get() const;
	};

END_NS
