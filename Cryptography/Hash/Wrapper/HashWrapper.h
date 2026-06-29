#ifndef NEBULA_HASHWRAPPER_H
#define NEBULA_HASHWRAPPER_H


#include "Type.h"

BEGIN_NS(ne::cryptography)
class HashWrapper
{
public:
	explicit HashWrapper() = default;
	virtual ~HashWrapper() = default;

public:
	[[nodiscard]] string_t GetHashFromString(string_t&& _text);
	void GetHashFromString(char_t* _buffer, size_t _bufferSize, lpcstr_t _text);

	[[nodiscard]] string_t GetHashFromFile(string_t&& _filePath);
	void GetHashFromFile(char_t* _buffer, size_t _bufferSize, lpcstr_t _filePath);

private:
	[[nodiscard]] virtual string_t OnGetHashFromString(string_t&& _string) = 0;
	[[nodiscard]] virtual string_t OnGetHashFromFile(FILE* _file) = 0;
}; 

END_NS

#endif //NEBULA_HASHWRAPPER_H