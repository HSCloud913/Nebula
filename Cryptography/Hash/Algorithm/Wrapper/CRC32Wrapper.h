#pragma once
#include <memory>
#include "Type.h"
#include "HashWrapper.h"
#include "Hash/Algorithm/CRC32.h"

BEGIN_NS(ne::crypto)
	class CRC32Wrapper :public HashWrapper
	{
	public:
		explicit CRC32Wrapper()
			: crc32(std::make_unique<CRC32>()) {}
		virtual ~CRC32Wrapper() = default;

	private:
		std::unique_ptr<CRC32> crc32;

	private:
		[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) override;
		[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) override;
	};

END_NS
