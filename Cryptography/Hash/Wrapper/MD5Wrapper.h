#ifndef MD5WRAPPER_H
#define MD5WRAPPER_H

#include <memory>
#include "Type.h"
#include "HashWrapper.h"
#include "Hash/MD5.h"

BEGIN_NS(ne::cryptography)
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

#endif //MD5WRAPPER_H
