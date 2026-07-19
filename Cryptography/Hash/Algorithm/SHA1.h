#pragma once
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
	/**
	 * @class SHA1
	 * @brief SHA-1 해시를 계산하는 저수준 알고리즘 구현입니다.
	 *
	 * Init() 후 AddBuffer()를 반복 호출하고 Get()으로 결과를 얻습니다.
	 */
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
		ulong_t sha1Value[Sha1HashValues]{};

	private:
		byte_t buffer[Sha1BlockSize]{};
		size_t bufferSize{ 0 };
		ulonglong_t length{ 0 };

	public:
		void_t Init();
		void_t AddBuffer(const void_t* _data, size_t _dataLength);
		[[nodiscard]] string_t Get();

	private:
		void_t ProcessBuffer();
		void_t ProcessBlock(const void_t* _data);
	};

END_NS
