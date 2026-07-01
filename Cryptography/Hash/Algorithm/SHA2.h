#pragma once
#include "Type.h"

BEGIN_NS(ne::crypto)
	class SHA2 final
	{
	public:
		enum class Type
		{
			SHA2_224 = 224,
			SHA2_256 = 256,
			SHA2_384 = 384,
			SHA2_512 = 512
		};

	public:
		explicit SHA2(const Type _type) : type(_type) {}
		~SHA2() = default;

	private:
		Type type;
		uint_t sha2Value32[8] = { 0, };
		ulonglong_t sha2Value64[8] = { 0, };

	private:
		byte_t buffer[128] = { 0, };
		ulonglong_t length = 0;
		uint_t currentLength = 0;

	public:
		void Init();
		void AddBuffer(const void_t* _data, size_t _dataLength);
		[[nodiscard]] string_t Get();
	};

END_NS
