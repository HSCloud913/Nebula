#pragma once
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
	/**
	 * @class AES
	 * @brief AES(128/192/256) 대칭키 암호화·복호화를 제공합니다.
	 *
	 * Create()로 키 타입과 키를 지정해 생성하며, CBC/ECB 두 모드의 암·복호화를 지원합니다.
	 */
	class AES final
	{
	public:
		enum class Type
		{
			AES_128,
			AES_192,
			AES_256
		};

	private:
		explicit AES(Type _type, string_t _key) noexcept;

	public:
		~AES() = default;

		NEBULA_NON_COPYABLE_MOVABLE(AES)

	private:
		Type type;
		string_t key;

	public:
		[[nodiscard]] static AES Create(const Type _type, string_t _key) noexcept { return AES{ _type, std::move(_key) }; }

	public:
		[[nodiscard]] string_t EncryptCBC(string_t&& _iv, string_t&& _plaintext) const;
		[[nodiscard]] string_t DecryptCBC(string_t&& _iv, string_t&& _ciphertext) const;
		[[nodiscard]] string_t EncryptECB(string_t&& _plaintext) const;
		[[nodiscard]] string_t DecryptECB(string_t&& _ciphertext) const;
	};

END_NS
