#pragma once
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
class HashWrapper
{
public:
	explicit HashWrapper() = default;
	virtual ~HashWrapper() = default;

public:
	[[nodiscard]] string_t GetHashFromString(string_t&& _text);
	void_t GetHashFromString(char_t* _buffer, size_t _bufferSize, lpcstr_t _text);

	[[nodiscard]] string_t GetHashFromFile(string_t&& _filePath);
	void_t GetHashFromFile(char_t* _buffer, size_t _bufferSize, lpcstr_t _filePath);

private:
	[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) = 0;
	[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) = 0;
}; 

END_NS
