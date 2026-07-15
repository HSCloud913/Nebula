#pragma once
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
	/**
	 * @class SHA2
	 * @brief SHA-2 계열(224/256/384/512) 해시를 계산하는 저수준 알고리즘 구현입니다.
	 *
	 * 생성 시 지정한 Type에 따라 224/256은 32비트 워드를, 384/512는 64비트 워드를 사용합니다.
	 * Init() 후 AddBuffer()를 반복 호출하고 Get()으로 결과를 얻습니다.
	 */
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
		explicit SHA2(const Type _type)
			: type(_type) {}
		~SHA2() = default;

	private:
		Type type;
		uint_t sha2Value32[8]{};
		ulonglong_t sha2Value64[8]{};

	private:
		byte_t buffer[128]{};
		ulonglong_t length{ 0 };
		uint_t currentLength{ 0 };

	public:
		void_t Init();
		void_t AddBuffer(const void_t* _data, size_t _dataLength);
		[[nodiscard]] string_t Get();
	};

END_NS
