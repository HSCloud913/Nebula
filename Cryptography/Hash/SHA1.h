#ifndef NEBULA_SHA1_H
#define NEBULA_SHA1_H

#include "Type.h"

BEGIN_NS(ne::cryptography)
	class SHA1 final
	{
	public:
		explicit SHA1() = default;
		~SHA1() = default;

	private:
		enum
		{
			Sha1BlockSize  = 512 / 8,
			Sha1HashBytes  = 20,
			Sha1HashValues = Sha1HashBytes / 4
		};

	private:
		ulong_t sha1Value[Sha1HashValues];

	private:
		byte_t buffer[Sha1BlockSize] = { 0, };
		size_t bufferSize = 0;
		ulonglong_t length = 0;

	public:
		void Init();
		void AddBuffer(const void_t* _data, size_t _dataLength);
		[[nodiscard]] string_t Get();

	private:
		void ProcessBuffer();
		void ProcessBlock(const void_t* _data);
	};

END_NS

#endif //NEBULA_SHA1_H
