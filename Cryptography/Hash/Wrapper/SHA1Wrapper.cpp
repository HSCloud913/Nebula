#include "SHA1Wrapper.h"



BEGIN_NS(ne::cryptography)
	string_t SHA1Wrapper::OnGetHashFromString(string_t&& _string)
	{
		sha1->Init();
		sha1->AddBuffer(_string.c_str(), _string.length());

		auto a = sha1->Get();
		return std::move(a);
	}

	string_t SHA1Wrapper::OnGetHashFromFile(FILE* _file)
	{
		sha1->Init();

		byte_t buffer[1024] = { 0, };
		size_t bufferLength = 0;

		do
		{
			bufferLength = fread(buffer, 1, 1024, _file);
			if (bufferLength > 0)
			{
				sha1->AddBuffer(buffer, bufferLength);
			}
		} while (bufferLength);

		return sha1->Get();
	}

END_NS
