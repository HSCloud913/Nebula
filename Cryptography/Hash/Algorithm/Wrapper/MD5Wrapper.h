#pragma once
#include <memory>
#include "Base/Type.h"
#include "Cryptography/Hash/Algorithm/Wrapper/HashWrapper.h"
#include "Cryptography/Hash/Algorithm/MD5.h"

BEGIN_NS(ne::crypto)
	/** @brief MD5 알고리즘을 HashWrapper 인터페이스로 노출하는 어댑터입니다. */
	class MD5Wrapper :public HashWrapper
	{
	public:
		explicit MD5Wrapper()
			: md5(std::make_unique<MD5>()) {}
		virtual ~MD5Wrapper() override = default;

	private:
		std::unique_ptr<MD5> md5;

	private:
		[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) override;
		[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) override;
	};

END_NS
