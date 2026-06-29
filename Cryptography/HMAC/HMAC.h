#ifndef NEBULA_HMAC_H
#define NEBULA_HMAC_H

#include "Type.h"
#include "Hash/Factory/HashFactory.h"

BEGIN_NS(ne::cryptography)

class HMAC final
{
	NEBULA_NON_COPYABLE_MOVABLE(HMAC)

private:
	explicit HMAC() = default;
	~HMAC() = default;

public:
	static string_t Compute(HashType _type, string_t&& _key, string_t&& _message);
};

END_NS

#endif //NEBULA_HMAC_H
