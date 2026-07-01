#pragma once
#include <memory>
#include "Type.h"
#include "HashWrapper.h"
#include "../Algorithm/SHA1.h"

BEGIN_NS(ne::crypto)
	class SHA1Wrapper :public HashWrapper
	{
	public:
		explicit SHA1Wrapper() : sha1(std::make_unique<SHA1>()) {}
		virtual ~SHA1Wrapper() = default;

	private:
		std::unique_ptr<SHA1> sha1;

	private:
		[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) override;
		[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) override;
	};

END_NS
