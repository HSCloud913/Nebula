#pragma once
#include <memory>
#include "Base/Type.h"
#include "Cryptography/Hash/Algorithm/Wrapper/HashWrapper.h"
#include "Cryptography/Hash/Algorithm/SHA3.h"

BEGIN_NS(ne::crypto)
	/** @brief SHA-3 알고리즘을 HashWrapper 인터페이스로 노출하는 어댑터입니다. 생성 시 지정한 Type(224/256/384/512)으로 동작합니다. */
	class SHA3Wrapper :public HashWrapper
	{
	public:
		explicit SHA3Wrapper(const SHA3::Type _type)
			: sha3(std::make_unique<SHA3>(_type)) {}
		virtual ~SHA3Wrapper() override = default;

	private:
		std::unique_ptr<SHA3> sha3;

	private:
		[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) override;
		[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) override;
	};

END_NS
