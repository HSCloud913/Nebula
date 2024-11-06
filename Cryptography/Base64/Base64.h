#ifndef BASE64_H
#define BASE64_H

#include "Type.h"

BEGIN_NS(ne::cryptography)

class Base64 final
{
private:
	explicit Base64() = default;
	~Base64() = default;

public:
	static void Encode(lpcstr_t _string, char_t* _buffer, size_t _bufferSize);
	static string_t Encode(string_t&& _string);

	static void Decode(lpcstr_t _string, char_t* _buffer, size_t _bufferSize);
	static string_t Decode(string_t&& _string);
};

END_NS

typedef ne::cryptography::Base64 NebulaBase64;

#endif //BASE64_H