#ifndef NEBULA_AES_H
#define NEBULA_AES_H

#include "Type.h"

BEGIN_NS(ne::cryptography)
	class AES final
	{
		NEBULA_NON_COPYABLE_MOVABLE(AES)

	private:
		explicit AES() = default;
		~AES() = default;

	public:
		enum class KeyType
		{
			AES128,
			AES192,
			AES256
		};

	public:
		static string_t EncryptCBC(KeyType _keyType, string_t&& _key, string_t&& _iv, string_t&& _plaintext);
		static string_t DecryptCBC(KeyType _keyType, string_t&& _key, string_t&& _iv, string_t&& _ciphertext);

		static string_t EncryptECB(KeyType _keyType, string_t&& _key, string_t&& _plaintext);
		static string_t DecryptECB(KeyType _keyType, string_t&& _key, string_t&& _ciphertext);
	};

END_NS

#endif //NEBULA_AES_H
