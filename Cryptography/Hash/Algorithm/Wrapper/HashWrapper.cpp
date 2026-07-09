#include "Cryptography/Hash/Algorithm/Wrapper/HashWrapper.h"

#include <cstring>



BEGIN_NS(ne::crypto)
	string_t HashWrapper::GetHashFromString(string_t&& _text)
	{
		return  OnGetHashFromString(std::move(_text));
	}

	void_t HashWrapper::GetHashFromString(char_t* _buffer, size_t _bufferSize, lpcstr_t _text)
	{
		strcpy_s(_buffer, _bufferSize, OnGetHashFromString(_text).c_str());
	}


	string_t HashWrapper::GetHashFromFile(string_t&& _filePath)
	{
		string_t result;

		FILE* file = nullptr;
		fopen_s(&file, _filePath.c_str(), "rb");

		if (file != nullptr)
		{
			result = OnGetHashFromFile(file);

			fclose(file);
		}

		return result;
	}

	void_t HashWrapper::GetHashFromFile(char_t* _buffer, size_t _bufferSize, lpcstr_t _filePath)
	{
		FILE* file = nullptr;

		fopen_s(&file, _filePath, "rb");
		if (file != nullptr)
		{
			strcpy_s(_buffer, _bufferSize, OnGetHashFromFile(file).c_str());

			fclose(file);
		}
	}

END_NS
