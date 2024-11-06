#include "MD5Wrapper.h"



BEGIN_NS(ne::cryptography)
	string_t MD5Wrapper::OnGetHashFromString(string_t&& _string)
	{
		md5->Init();
		md5->AddBuffer(_string.c_str(), _string.length());

		return md5->Get();
	}

	string_t MD5Wrapper::OnGetHashFromFile(FILE* _file)
	{
		md5->Init();

		byte_t buffer[1024] = { 0, };
		size_t bufferLength = 0;

		do
		{
			bufferLength = fread(buffer, 1, 1024, _file);
			if (bufferLength > 0)
			{
				md5->AddBuffer(buffer, bufferLength);
			}
		} while (bufferLength);

		return md5->Get();
	}

END_NS
