#pragma once
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
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
		explicit SHA3(const Type _type)
			: type(_type) {}
		~SHA3() = default;

	private:
		enum
		{
			StateSize    = 1600 / (8 * 8),
			MaxBlockSize = 200 - 2 * (224 / 8)
		};

	private:
		Type type;
		ulonglong_t sha3Value[StateSize] = {};

	private:
		byte_t buffer[MaxBlockSize]{};
		size_t bufferSize{ 0 };
		size_t blockSize{ 0 };
		ulong_t length{ 0 };

	public:
		void_t Init();
		void_t AddBuffer(const void_t* _data, size_t _dataLength);
		[[nodiscard]] string_t Get();

	private:
		void_t ProcessBuffer();
		void_t ProcessBlock(const void_t* _data);
	};

END_NS
