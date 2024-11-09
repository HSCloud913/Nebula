#ifndef CRC32_H
#define CRC32_H

#include "Type.h"

BEGIN_NS(ne::cryptography)
	class CRC32 final
	{
	public:
		explicit CRC32() = default;
		~CRC32() = default;

	private:
		uint_t hash = 0;

	public:
		void Init();
		void AddBuffer(const void* _data, size_t _dataLen);
		[[nodiscard]] string_t Get() const;
	};

END_NS

#endif //CRC32_H
