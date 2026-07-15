#pragma once
#include <memory>
#include "Base/Type.h"
#include "Cryptography/Hash/Algorithm/Wrapper/HashWrapper.h"
#include "Cryptography/Hash/Algorithm/CRC32.h"

BEGIN_NS(ne::crypto)
	/** @brief CRC32 알고리즘을 HashWrapper 인터페이스로 노출하는 어댑터입니다. */
	class CRC32Wrapper :public HashWrapper
	{
	public:
		explicit CRC32Wrapper()
			: crc32(std::make_unique<CRC32>()) {}
		virtual ~CRC32Wrapper() override = default;

	private:
		std::unique_ptr<CRC32> crc32;

	private:
		[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) override;
		[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) override;
	};

END_NS
