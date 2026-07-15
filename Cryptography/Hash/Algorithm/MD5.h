#pragma once
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
	/** @brief MD5 해시를 계산하는 저수준 알고리즘 구현입니다. Init() 후 AddBuffer()를 반복 호출하고 Get()으로 결과를 얻습니다. */
	class MD5 final
	{
	public:
		explicit MD5() = default;
		~MD5() = default;

	private:
		enum
		{
			MD5BlockSize  = 512 / 8,
			MD5HashBytes  = 16,
			MD5HashValues = MD5HashBytes / 4
		};

	private:
		uint_t md5Value[MD5HashValues]{};

	private:
		byte_t buffer[MD5BlockSize]{};
		size_t bufferSize{ 0 };
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
