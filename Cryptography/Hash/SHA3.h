#ifndef NEBULA_SHA3_H
#define NEBULA_SHA3_H

#include "Type.h"

BEGIN_NS(ne::cryptography)
	class SHA3 final
	{
	public:
		enum class Type
		{
			SHA3_224 = 224,
			SHA3_256 = 256,
			SHA3_384 = 384,
			SHA3_512 = 512
		};

	public:
		explicit SHA3(Type _type) : type(_type) {}
		~SHA3() = default;

	private:
		enum
		{
			StateSize    = 1600 / (8 * 8),
			MaxBlockSize = 200 - 2 * (224 / 8)
		};

	private:
		Type type;
		ulonglong_t sha3Value[StateSize] = { 0, };

	private:
		byte_t buffer[MaxBlockSize];
		size_t bufferSize = 0;
		size_t blockSize = 0;
		ulong_t length = 0;

	public:
		void Init();
		void AddBuffer(const void_t* _data, size_t _dataLength);
		[[nodiscard]] string_t Get();

	private:
		void ProcessBuffer();
		void ProcessBlock(const void_t* _data);
	};

END_NS

#endif //NEBULA_SHA3_H
