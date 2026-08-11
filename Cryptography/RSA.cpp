#include "Cryptography/RSA.h"

#include <array>
#include <random>
#include <utility>
#include <vector>
#include "Cryptography/AES.h"
#include "Cryptography/HMAC.h"
#include "Cryptography/Kdf.h"
#include "Cryptography/Internal/ConstantTime.h"
#include "Cryptography/Internal/Math/BigInt.h"
#include "Util/Hex.h"
#include "Util/SecureRandom.h"
#include "Util/SecureWipe.h"



namespace ne::crypto
{
	using internal::BigInt; // 구현 세부(Internal) — 공개 표면(RSA.h)에는 노출되지 않는다

	namespace
	{
		// EMSA-PKCS1-v1_5 의 DigestInfo DER 접두사(RFC 8017 9.2). 해시별로 고정된 바이트열이다.
		string_view_t DigestInfoPrefix(const HashType _type) noexcept
		{
			switch (_type)
			{
				case HashType::SHA2_256:
					return string_view_t("\x30\x31\x30\x0d\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x01\x05\x00\x04\x20", 19);
				case HashType::SHA2_384:
					return string_view_t("\x30\x41\x30\x0d\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x02\x05\x00\x04\x30", 19);
				case HashType::SHA2_512:
					return string_view_t("\x30\x51\x30\x0d\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x03\x05\x00\x04\x40", 19);
				case HashType::SHA1:
					return string_view_t("\x30\x21\x30\x09\x06\x05\x2b\x0e\x03\x02\x1a\x05\x00\x04\x14", 15);
				default:
					return {}; // 서명에 쓸 수 없는 해시(CRC32/MD5/SHA3 — DER OID 미등재)
			}
		}

		// 메시지를 해시해 EMSA-PKCS1-v1_5 인코딩 블록을 만든다: 0x00 || 0x01 || 0xFF.. || 0x00 || DigestInfo
		CryptoResult<string_t> BuildSignatureBlock(const string_view_t _message, const HashType _hashType, const std::size_t _keyLength)
		{
			using R = CryptoResult<string_t>;

			const string_view_t prefix = DigestInfoPrefix(_hashType);
			if (prefix.empty()) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "hash type is not usable for PKCS#1 v1.5 signatures" });

			const auto digest = ne::util::Hex::Decode(Hash(_hashType, _message));
			if (!digest) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "hash output is not valid hex" });

			const std::size_t infoLength = prefix.size() + digest->size();
			if (_keyLength < infoLength + 11) return R::Error(CryptoError{ CryptoErrorKind::MESSAGE_TOO_LARGE, "RSA key too small for this signature hash" });

			string_t block;
			block.reserve(_keyLength);
			block += '\x00';
			block += '\x01';
			block.append(_keyLength - infoLength - 3, '\xFF'); // PS: 최소 8바이트가 보장된다(위 검사)
			block += '\x00';
			block.append(prefix);
			block.append(*digest);

			return R::Ok(std::move(block));
		}

		// "TAG:<hex>:<hex>" 를 분해한다. 태그 불일치·필드 수 오류·비16진이면 실패.
		CryptoResult<std::array<string_t, 2>> ParseKeyText(const string_view_t _text, const string_view_t _tag)
		{
			using R = CryptoResult<std::array<string_t, 2>>;

			if (!_text.starts_with(_tag)) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "unexpected key tag" });

			const string_view_t body = _text.substr(_tag.size());
			const std::size_t separator = body.find(':');
			if (separator == string_view_t::npos) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "malformed serialized key" });

			const string_view_t first = body.substr(0, separator);
			const string_view_t second = body.substr(separator + 1);
			if (first.empty() || second.empty()) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "malformed serialized key" });

			// 16진 유효성만 확인한다(값 자체가 유효한 모듈러스인지는 여기서 판단하지 않음).
			if (!ne::util::Hex::Decode(first.size() % 2 == 0 ? first : string_t("0").append(first)) ||
				!ne::util::Hex::Decode(second.size() % 2 == 0 ? second : string_t("0").append(second)))
			{
				return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "serialized key is not hexadecimal" });
			}

			return R::Ok(std::array<string_t, 2>{ string_t(first), string_t(second) });
		}

		constexpr string_view_t PublicKeyTag = "NRSA1-PUB:";
		constexpr string_view_t PrivateKeyTag = "NRSA1-PRV:";
	}

	RSAPrivateKey::~RSAPrivateKey() { ne::util::SecureWipe(d); }

	CryptoResult<string_t> RSAPublicKey::Encrypt(const string_view_t _plainText) const
	{
		using R = CryptoResult<string_t>;

		const BigInt bn = BigInt::FromHex(n);
		const BigInt be = BigInt::FromHex(e);

		const size_t keyLength = (bn.BitLength() + 7) / 8;
		if (keyLength < 11 || _plainText.size() > keyLength - 11) return R::Error(CryptoError{ CryptoErrorKind::MESSAGE_TOO_LARGE, "plaintext too large for RSA key size" });

		ne::util::SecureRandom rng; // PKCS#1 v1.5 패딩 난수 — CSPRNG 사용(URBG 모델링이라 distribution 과 호환)
		std::uniform_int_distribution<uint_t> dist(1, 255);

		const size_t psLen = keyLength - 3 - _plainText.size();

		// PKCS#1 v1.5 패딩 레이아웃: 0x00 0x02 PS(0이 아닌 랜덤 바이트로 채운 패딩, 길이 가변) 0x00 M(원본 메시지)
		// - 0x00: 맨 앞 바이트가 0이면 BigInt로 변환해도 항상 keyLength 크기보다 작게 해석되지 않도록 보장.
		// - 0x02: 블록 타입(암호화용, 서명은 0x01) 식별자.
		// - PS: 0이 아닌 랜덤 바이트로 채워 최소 8바이트 이상 패딩 → 평문을 무작위화(같은 평문도 매번 다른 암호문)하고
		//   PS 중간에 0x00이 섞이면 안 되므로 1~255 범위에서만 뽑는다.
		// - 0x00: PS와 메시지를 구분하는 분리자.
		string_t em;
		em.reserve(keyLength);
		em += '\x00';
		em += '\x02';
		for (size_t i = 0; i < psLen; ++i) em += static_cast<char_t>(dist(rng));
		em += '\x00';
		em += _plainText;

		const BigInt m = BigInt::FromBytes(em);
		const BigInt c = BigInt::ModPow(m, be, bn);

		return R::Ok(c.ToBytes(keyLength));
	}



	CryptoResult<string_t> RSAPrivateKey::Decrypt(const string_view_t _cipherText) const
	{
		using R = CryptoResult<string_t>;

		const BigInt bn = BigInt::FromHex(n);
		const BigInt bd = BigInt::FromHex(d);
		const size_t keyLen = (bn.BitLength() + 7) / 8;

		const BigInt c = BigInt::FromBytes(string_t(_cipherText));
		const BigInt m = BigInt::ModPow(c, bd, bn);
		const string_t em = m.ToBytes(keyLen);

		if (em.size() < 11 || static_cast<byte_t>(em[0]) != 0x00 || static_cast<byte_t>(em[1]) != 0x02) return R::Error(CryptoError{ CryptoErrorKind::MALFORMED_PADDING, "invalid PKCS#1 v1.5 block" });

		size_t sep = 2;
		while (sep < em.size() && em[sep] != '\x00') ++sep;

		if (sep == em.size()) return R::Error(CryptoError{ CryptoErrorKind::MALFORMED_PADDING, "padding separator not found" });

		return R::Ok(em.substr(sep + 1));
	}



	RSAKeyPair RSAKeyPair::Generate(const KeySize _keySize)
	{
		const size_t halfBits = static_cast<size_t>(_keySize) / 2;

		ne::util::SecureRandom rng; // RSA 소수 p, q 생성 — 반드시 CSPRNG(예측 가능 시 키 복구 가능)

		BigInt n, d;
		const BigInt e(65537u);

		while (true)
		{
			BigInt p = BigInt::RandomPrime(halfBits, rng);
			BigInt q = BigInt::RandomPrime(halfBits, rng);
			if (p == q) continue;

			n = p * q;
			if (n.BitLength() != static_cast<size_t>(_keySize)) continue;

			const BigInt phi = (p - BigInt(1u)) * (q - BigInt(1u));
			if (BigInt::Gcd(e, phi) != BigInt(1u)) continue;

			d = BigInt::ModInverse(e, phi);
			break;
		}

		RSAKeyPair keyPair;
		keyPair.publicKey.n = n.ToHex();
		keyPair.publicKey.e = e.ToHex();
		keyPair.privateKey.n = n.ToHex();
		keyPair.privateKey.d = d.ToHex();

		return keyPair;
	}



	// ───────────────────────── 서명 / 검증 (RSASSA-PKCS1-v1_5) ─────────────────────────

	CryptoResult<string_t> RSAPrivateKey::Sign(const string_view_t _message, const HashType _hashType) const
	{
		using R = CryptoResult<string_t>;

		const BigInt bn = BigInt::FromHex(n);
		const BigInt bd = BigInt::FromHex(d);
		const size_t keyLength = (bn.BitLength() + 7) / 8;

		auto block = BuildSignatureBlock(_message, _hashType, keyLength);
		if (block.IsError()) return R::Error(std::move(block.Error()).Context("[RSA/Sign]"));

		const BigInt m = BigInt::FromBytes(block.Value());
		const BigInt s = BigInt::ModPow(m, bd, bn);

		return R::Ok(s.ToBytes(keyLength));
	}

	bool_t RSAPublicKey::Verify(const string_view_t _message, const string_view_t _signature, const HashType _hashType) const
	{
		const BigInt bn = BigInt::FromHex(n);
		const BigInt be = BigInt::FromHex(e);
		const size_t keyLength = (bn.BitLength() + 7) / 8;

		if (_signature.size() != keyLength) return false;

		auto expected = BuildSignatureBlock(_message, _hashType, keyLength);
		if (expected.IsError()) return false;

		const BigInt s = BigInt::FromBytes(string_t(_signature));
		const BigInt m = BigInt::ModPow(s, be, bn);

		// 복원한 블록과 기대 블록을 상수시간으로 비교한다(어디가 어긋났는지 흘리지 않음).
		return internal::ConstantTimeEquals(m.ToBytes(keyLength), expected.Value());
	}



	// ───────────────────────── 직렬화 ─────────────────────────

	string_t RSAPublicKey::Serialize() const { return string_t(PublicKeyTag) + n + ":" + e; }

	CryptoResult<RSAPublicKey> RSAPublicKey::Deserialize(const string_view_t _text)
	{
		using R = CryptoResult<RSAPublicKey>;

		auto parsed = ParseKeyText(_text, PublicKeyTag);
		if (parsed.IsError()) return R::Error(std::move(parsed.Error()).Context("[RSA/PublicKey]"));

		return R::Ok(RSAPublicKey{ std::move(parsed.Value()[0]), std::move(parsed.Value()[1]) });
	}

	string_t RSAPrivateKey::Serialize() const { return string_t(PrivateKeyTag) + n + ":" + d; }

	CryptoResult<RSAPrivateKey> RSAPrivateKey::Deserialize(const string_view_t _text)
	{
		using R = CryptoResult<RSAPrivateKey>;

		auto parsed = ParseKeyText(_text, PrivateKeyTag);
		if (parsed.IsError()) return R::Error(std::move(parsed.Error()).Context("[RSA/PrivateKey]"));

		RSAPrivateKey key;
		key.n = std::move(parsed.Value()[0]);
		key.d = std::move(parsed.Value()[1]);

		return R::Ok(std::move(key));
	}



	// ───────────────────────── 하이브리드 봉인 ─────────────────────────

	namespace
	{
		// 하이브리드 봉투 포맷 v1: version(1) || wrappedKeyLength(2 BE) || wrappedKey || iv(16) || ciphertext || mac(32)
		constexpr byte_t HybridVersion = 0x01;
		constexpr std::size_t VersionLength = 1;
		constexpr std::size_t WrappedLengthField = 2;
		constexpr std::size_t MacLength = 32; // HMAC-SHA256

		// 임시 마스터 키에서 용도별 서브키를 뽑는다(하나의 키를 암호화·MAC 에 겸용하지 않기 위함).
		constexpr string_view_t EncryptionLabel = "nebula-rsa-hybrid-v1-enc";
		constexpr string_view_t MacLabel = "nebula-rsa-hybrid-v1-mac";

		struct HybridSubkeys
		{
			string_t encryption;
			string_t mac;
		};

		// 마스터 키는 CSPRNG 로 뽑은 균일 무작위 값이므로 HKDF-Extract 를 건너뛰고 Expand 만 쓴다
		// (RFC 5869 §3.3). 라벨(info)이 달라서 두 서브키는 서로 독립이다.
		CryptoResult<HybridSubkeys> DeriveHybridSubkeys(const string_view_t _master)
		{
			using R = CryptoResult<HybridSubkeys>;

			auto encryption = HkdfExpand(HashType::SHA2_256, _master, EncryptionLabel, 32);
			if (encryption.IsError()) return R::Error(std::move(encryption.Error()));

			auto mac = HkdfExpand(HashType::SHA2_256, _master, MacLabel, 32);
			if (mac.IsError()) return R::Error(std::move(mac.Error()));

			return R::Ok(HybridSubkeys{ std::move(encryption.Value()), std::move(mac.Value()) });
		}

		string_t EncodeBigEndian16(const std::size_t _value)
		{
			string_t out;
			out += static_cast<char_t>((_value >> 8) & 0xFF);
			out += static_cast<char_t>(_value & 0xFF);

			return out;
		}
	}

	CryptoResult<string_t> RsaSeal(const RSAPublicKey& _publicKey, const string_view_t _plaintext, const string_view_t _associatedData)
	{
		using R = CryptoResult<string_t>;

		// 1) 임시 대칭 마스터 키를 CSPRNG 로 뽑는다(이 봉투 한 건 전용).
		string_t master(32, '\0');
		ne::util::SecureRandom random;
		if (!random.Fill(master.data(), master.size())) return R::Error(CryptoError{ CryptoErrorKind::RANDOM_FAILURE, "failed to generate hybrid session key" });

		auto subkeys = DeriveHybridSubkeys(master);
		if (subkeys.IsError()) return R::Error(std::move(subkeys.Error()).Context("[RsaSeal]"));

		// 2) 마스터 키만 RSA 로 감싼다(작으므로 RSA 상한에 걸리지 않는다).
		auto wrapped = _publicKey.Encrypt(master);
		ne::util::SecureWipe(master);
		if (wrapped.IsError()) return R::Error(std::move(wrapped.Error()).Context("[RsaSeal]"));

		// 3) 본문은 AES-256-CBC(매번 새 IV)로 암호화한다.
		auto cipher = AES::Create(AES::Type::AES_256, subkeys.Value().encryption);
		if (cipher.IsError()) return R::Error(std::move(cipher.Error()).Context("[RsaSeal]"));

		auto encrypted = cipher.Value().EncryptCBC(_plaintext);
		if (encrypted.IsError()) return R::Error(std::move(encrypted.Error()).Context("[RsaSeal]"));

		// 4) version..ciphertext 전체(+aad)에 MAC 을 건다(Encrypt-then-MAC).
		string_t envelope;
		envelope += static_cast<char_t>(HybridVersion);
		envelope += EncodeBigEndian16(wrapped.Value().size());
		envelope += wrapped.Value();
		envelope += encrypted.Value().iv;
		envelope += encrypted.Value().ciphertext;

		auto mac = HMACKey::Create(HashType::SHA2_256, subkeys.Value().mac);
		if (mac.IsError()) return R::Error(std::move(mac.Error()).Context("[RsaSeal]"));

		envelope += mac.Value().GenerateBytes(string_t(envelope).append(_associatedData));

		return R::Ok(std::move(envelope));
	}

	CryptoResult<string_t> RsaOpen(const RSAPrivateKey& _privateKey, const string_view_t _sealed, const string_view_t _associatedData)
	{
		using R = CryptoResult<string_t>;

		if (_sealed.size() < VersionLength + WrappedLengthField + AES::BlockSize + MacLength) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "sealed envelope is truncated" });
		if (static_cast<byte_t>(_sealed[0]) != HybridVersion) return R::Error(CryptoError{ CryptoErrorKind::UNSUPPORTED_VERSION });

		const std::size_t wrappedLength = (static_cast<byte_t>(_sealed[1]) << 8) | static_cast<byte_t>(_sealed[2]);
		const std::size_t headerLength = VersionLength + WrappedLengthField + wrappedLength;
		if (_sealed.size() < headerLength + AES::BlockSize + MacLength) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "wrapped key length does not fit the envelope" });

		const std::size_t bodyLength = _sealed.size() - MacLength;
		const string_view_t body = _sealed.substr(0, bodyLength);
		const string_view_t presentedMac = _sealed.substr(bodyLength);
		const string_view_t wrappedKey = _sealed.substr(VersionLength + WrappedLengthField, wrappedLength);
		const string_view_t iv = _sealed.substr(headerLength, AES::BlockSize);
		const string_view_t ciphertext = body.substr(headerLength + AES::BlockSize);

		if (ciphertext.empty() || ciphertext.size() % AES::BlockSize != 0) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "ciphertext is not block aligned" });

		// 1) 대칭 마스터 키를 RSA 로 복원한다. 실패(패딩 오류 포함)는 인증 실패로 뭉갠다 — 패딩 오라클 방지.
		auto master = _privateKey.Decrypt(wrappedKey);
		if (master.IsError() || master.Value().size() != 32) return R::Error(CryptoError{ CryptoErrorKind::AUTHENTICATION_FAILED });

		auto subkeys = DeriveHybridSubkeys(master.Value());
		ne::util::SecureWipe(master.Value());
		if (subkeys.IsError()) return R::Error(std::move(subkeys.Error()).Context("[RsaOpen]"));

		// 2) MAC 을 상수시간으로 먼저 검증한다 — 실패하면 본문 복호를 시도하지 않는다.
		auto mac = HMACKey::Create(HashType::SHA2_256, subkeys.Value().mac);
		if (mac.IsError()) return R::Error(std::move(mac.Error()).Context("[RsaOpen]"));

		if (!internal::ConstantTimeEquals(mac.Value().GenerateBytes(string_t(body).append(_associatedData)), presentedMac))
		{
			return R::Error(CryptoError{ CryptoErrorKind::AUTHENTICATION_FAILED });
		}

		// 3) 인증된 뒤에야 본문을 복호한다.
		auto cipher = AES::Create(AES::Type::AES_256, subkeys.Value().encryption);
		if (cipher.IsError()) return R::Error(std::move(cipher.Error()).Context("[RsaOpen]"));

		auto plaintext = cipher.Value().DecryptCBC(iv, ciphertext);
		if (plaintext.IsError()) return R::Error(CryptoError{ CryptoErrorKind::AUTHENTICATION_FAILED }); // MAC 통과 후 실패는 구현 결함 — 패딩 상태를 노출하지 않는다

		return R::Ok(std::move(plaintext.Value()));
	}
}
