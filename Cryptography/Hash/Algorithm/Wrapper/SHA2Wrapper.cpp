#include "Cryptography/Hash/Algorithm/Wrapper/SHA2Wrapper.h"



BEGIN_NS(ne::crypto)
	string_t SHA2Wrapper::OnGetHashFromString(string_t&& _string)
	{
		sha2->Init();
		sha2->AddBuffer(_string.c_str(), _string.length());

		return sha2->Get();
	}

	string_t SHA2Wrapper::OnGetHashFromFile(FILE* _file)
	{
		sha2->Init();

		byte_t buffer[1024] = { 0, };
		size_t bufferLength = 0;

		do
		{
			bufferLength = fread(buffer, 1, 1024, _file);
			if (bufferLength > 0) { sha2->AddBuffer(buffer, bufferLength); }
		} while (bufferLength);

		return sha2->Get();
	}

END_NS
