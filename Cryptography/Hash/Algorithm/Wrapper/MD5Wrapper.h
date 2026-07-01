#pragma once
#include <memory>
#include "Type.h"
#include "HashWrapper.h"
#include "../Algorithm/MD5.h"

BEGIN_NS(ne::crypto)
	class MD5Wrapper :public HashWrapper
	{
	public:
		explicit MD5Wrapper() : md5(std::make_unique<MD5>()) {}
		virtual ~MD5Wrapper() = default;

	private:
		std::unique_ptr<MD5> md5;

	private:
		[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) override;
		[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) override;
	};

END_NS
