#ifndef NEBULA_SHA1WRAPPER_H
#define NEBULA_SHA1WRAPPER_H

#include <memory>
#include "Type.h"
#include "HashWrapper.h"
#include "Hash/SHA1.h"

BEGIN_NS(ne::cryptography)
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

#endif //NEBULA_SHA1WRAPPER_H
