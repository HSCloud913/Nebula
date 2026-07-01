#include "CRC32Wrapper.h"

#include "StringFormat.h"



BEGIN_NS(ne::crypto)
	string_t CRC32Wrapper::OnGetHashFromString(string_t&& _string)
	{
		crc32->Init();
		crc32->AddBuffer(_string.c_str(), _string.length());

		return std::move(StringFormat::Upper(crc32->Get()));
	}

	string_t CRC32Wrapper::OnGetHashFromFile(FILE* _file)
	{
		crc32->Init();

		byte_t buffer[1024] = { 0, };
		size_t bufferLength = 0;

		do
		{
			bufferLength = fread(buffer, 1, 1024, _file);
			if (bufferLength > 0)
			{
				crc32->AddBuffer(buffer, bufferLength);
			}
		} while (bufferLength);

		return std::move(StringFormat::Upper(crc32->Get()));
	}

END_NS
