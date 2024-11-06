#include "HashWrapper.h"

#include <cstring>



BEGIN_NS(ne::cryptography)
	string_t HashWrapper::GetHashFromString(string_t&& _text)
	{
		return  OnGetHashFromString(std::move(_text));
	}

	void HashWrapper::GetHashFromString(char_t* _buf, size_t _bufSize, lpcstr_t _text)
	{
		strcpy_s(_buf, _bufSize, OnGetHashFromString(_text).c_str());
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

	void HashWrapper::GetHashFromFile(char_t* _buf, size_t _bufSize, lpcstr_t _filePath)
	{
		FILE* file = nullptr;

		fopen_s(&file, _filePath, "rb");
		if (file != nullptr)
		{
			strcpy_s(_buf, _bufSize, OnGetHashFromFile(file).c_str());

			fclose(file);
		}
	}

END_NS
