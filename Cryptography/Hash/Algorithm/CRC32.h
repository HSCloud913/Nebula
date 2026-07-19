#pragma once
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
	/**
	 * @class CRC32
	 * @brief CRC32 체크섬을 계산하는 저수준 알고리즘 구현입니다.
	 *
	 * Init() 후 AddBuffer()를 반복 호출하고 Get()으로 결과를 얻습니다.
	 */
	class CRC32 final
	{
	public:
		explicit CRC32() = default;
		~CRC32() = default;

	private:
		uint_t hash = 0;

	public:
		void_t Init() { hash = 0; }
		void_t AddBuffer(const void_t* _data, size_t _dataLength);
		[[nodiscard]] string_t Get() const;
	};

END_NS
