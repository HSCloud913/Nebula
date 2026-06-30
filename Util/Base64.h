#ifndef NEBULA_BASE64_H
#define NEBULA_BASE64_H

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

	static string_t EncodeURL(string_t&& _string);
	static string_t DecodeURL(string_t&& _string);
};

END_NS

#endif //NEBULA_BASE64_H