#include "Cryptography/Hash/Algorithm/Wrapper/SHA3Wrapper.h"



BEGIN_NS(ne::crypto)
	string_t SHA3Wrapper::OnGetHashFromString(string_t&& _string)
	{
		sha3->Init();
		sha3->AddBuffer(_string.c_str(), _string.length());

		return sha3->Get();
	}

	string_t SHA3Wrapper::OnGetHashFromFile(FILE* _file)
	{
		sha3->Init();

		byte_t buffer[1024] = { 0, };
		size_t bufferLength = 0;

		do
		{
			bufferLength = fread(buffer, 1, 1024, _file);
			if (bufferLength > 0)
			{
				sha3->AddBuffer(buffer, bufferLength);
			}
		} while (bufferLength);

		return sha3->Get();
	}

END_NS
