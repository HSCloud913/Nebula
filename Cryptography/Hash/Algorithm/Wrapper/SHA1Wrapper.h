#pragma once
#include <memory>
#include "Base/Type.h"
#include "Cryptography/Hash/Algorithm/Wrapper/HashWrapper.h"
#include "Cryptography/Hash/Algorithm/SHA1.h"

BEGIN_NS(ne::crypto)
	/** @brief SHA-1 알고리즘을 HashWrapper 인터페이스로 노출하는 어댑터입니다. */
	class SHA1Wrapper :public HashWrapper
	{
	public:
		explicit SHA1Wrapper()
			: sha1(std::make_unique<SHA1>()) {}
		virtual ~SHA1Wrapper() override = default;

	private:
		std::unique_ptr<SHA1> sha1;

	private:
		[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) override;
		[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) override;
	};

END_NS
