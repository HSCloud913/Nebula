#pragma once
#include <memory>
#include "Type.h"
#include "HashWrapper.h"
#include "Hash/Algorithm/SHA2.h"

BEGIN_NS(ne::crypto)
	class SHA2Wrapper :public HashWrapper
	{
	public:
		explicit SHA2Wrapper(SHA2::Type _type) : sha2(std::make_unique<SHA2>(_type)) {}
		virtual ~SHA2Wrapper() = default;

	private:
		std::unique_ptr<SHA2> sha2;

	private:
		[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) override;
		[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) override;
	};

END_NS
