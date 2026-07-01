#pragma once
#include <memory>
#include "Type.h"
#include "HashWrapper.h"
#include "../Algorithm/SHA3.h"

BEGIN_NS(ne::crypto)
	class SHA3Wrapper :public HashWrapper
	{
	public:
		explicit SHA3Wrapper(SHA3::Type _type) : sha3(std::make_unique<SHA3>(_type)) {}
		virtual ~SHA3Wrapper() = default;

	private:
		std::unique_ptr<SHA3> sha3;

	private:
		[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) override;
		[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) override;
	};

END_NS
