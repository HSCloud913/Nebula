#ifndef SHA3WRAPPER_H
#define SHA3WRAPPER_H

#include <memory>
#include "Type.h"
#include "HashWrapper.h"
#include "Hash/SHA3.h"

BEGIN_NS(ne::cryptography)
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

#endif //SHA3WRAPPER_H
