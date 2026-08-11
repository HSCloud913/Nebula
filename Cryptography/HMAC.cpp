#include "Cryptography/HMAC.h"

#include <cstdio>
#include <utility>
#include "Cryptography/Internal/HashFactory.h"
#include "Cryptography/Internal/ConstantTime.h"
#include "Util/Hex.h"
#include "Util/SecureWipe.h"



namespace ne::crypto
{
	namespace
	{
		std::size_t GetBlockSize(const HashType _type)
		{
			switch (_type)
			{
				case HashType::SHA2_384:
				case HashType::SHA2_512:
					return 128;
				case HashType::SHA3_224:
					return 144;
				case HashType::SHA3_256:
					return 136;
				case HashType::SHA3_384:
					return 104;
				case HashType::SHA3_512:
					return 72;
				default:
					return 64;
			}
		}

		// 해시 구현은 항상 유효한 소문자 hex 를 내보내므로 디코딩이 실패할 수 없다(실패 시 빈 값).
		ne::string_t HashBytes(const HashType _type, const ne::string_view_t _data)
		{
			const ne::string_t hex = internal::HashFactory::Create(_type)->FromString(_data);

			return ne::util::Hex::Decode(hex).value_or(ne::string_t{});
		}
	}



	HMACKey::HMACKey(const HashType _type, string_t&& _ipad, string_t&& _opad) noexcept
		: type(_type)
		, ipad(std::move(_ipad))
		, opad(std::move(_opad)) {}

	HMACKey::~HMACKey()
	{
		ne::util::SecureWipe(ipad);
		ne::util::SecureWipe(opad);
	}



	CryptoResult<HMACKey> HMACKey::Create(const HashType _type, const string_view_t _key)
	{
		using R = CryptoResult<HMACKey>;

		// CRC32 는 암호학적 해시가 아니라 체크섬 — HMAC 으로 감싸도 위조를 막지 못한다.
		if (_type == HashType::CRC32) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "CRC32 is a checksum, not a cryptographic hash — unusable for HMAC" });

		const std::size_t blockSize = GetBlockSize(_type);

		// RFC 2104: 키가 블록보다 길면 해시로 줄이고, 짧으면 0 으로 오른쪽 패딩한다.
		string_t normalizedKey = (_key.size() > blockSize) ? HashBytes(_type, _key) : string_t(_key);
		normalizedKey.resize(blockSize, '\0');

		string_t ipad(blockSize, '\0');
		string_t opad(blockSize, '\0');
		for (std::size_t i = 0; i < blockSize; ++i)
		{
			ipad[i] = static_cast<char_t>(static_cast<byte_t>(normalizedKey[i]) ^ 0x36);
			opad[i] = static_cast<char_t>(static_cast<byte_t>(normalizedKey[i]) ^ 0x5C);
		}

		ne::util::SecureWipe(normalizedKey); // 정규화된 원본 키는 더 이상 필요 없다

		return R::Ok(HMACKey(_type, std::move(ipad), std::move(opad)));
	}



	string_t HMACKey::Generate(const string_view_t _message) const
	{
		return ne::util::Hex::Encode(GenerateBytes(_message));
	}

	string_t HMACKey::GenerateBytes(const string_view_t _message) const
	{
		const string_t inner = HashBytes(type, ipad + string_t(_message));

		return HashBytes(type, opad + inner);
	}

	bool_t HMACKey::Verify(const string_view_t _message, const string_view_t _expectedMac) const
	{
		return internal::ConstantTimeEquals(Generate(_message), _expectedMac);
	}

	CryptoResult<HmacStream> HMACKey::BeginStream() const
	{
		using R = CryptoResult<HmacStream>;

		auto inner = internal::HashFactory::Create(type);
		if (inner == nullptr) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "unsupported hash type" });

		// 내부 해시는 ipad 를 먼저 먹인 상태에서 시작한다 — 이후 Update 가 메시지를 이어 붙인다.
		inner->Init();
		inner->Update(ipad);

		return R::Ok(HmacStream{ type, ipad, opad, std::move(inner) });
	}

	CryptoResult<string_t> HMACKey::GenerateFromFile(const string_view_t _path) const
	{
		using R = CryptoResult<string_t>;

		auto stream = BeginStream();
		if (stream.IsError()) return R::Error(std::move(stream.Error()));

		const string_t path(_path); // fopen 은 널 종단 경로 필요

#if defined(_WIN32)
		FILE* file = nullptr;
		if (::fopen_s(&file, path.c_str(), "rb") != 0) file = nullptr;
#else
		FILE* file = std::fopen(path.c_str(), "rb");
#endif
		if (file == nullptr) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "cannot open file for HMAC" });

		char_t buffer[4096];
		std::size_t length = 0;
		do
		{
			length = std::fread(buffer, 1, sizeof(buffer), file);
			if (length > 0) stream.Value().Update(string_view_t(buffer, length));
		} while (length > 0);

		std::fclose(file);

		return R::Ok(stream.Value().Final());
	}



	// ───────────────────────── HmacStream (증분) ─────────────────────────

	HmacStream::HmacStream(const HashType _type, string_t _ipad, string_t _opad, std::unique_ptr<internal::HashWrapper> _inner)
		: type(_type)
		, ipad(std::move(_ipad))
		, opad(std::move(_opad))
		, inner(std::move(_inner)) {}

	HmacStream::~HmacStream()
	{
		ne::util::SecureWipe(ipad);
		ne::util::SecureWipe(opad);
	}

	HmacStream::HmacStream(HmacStream&&) noexcept = default;
	HmacStream& HmacStream::operator=(HmacStream&&) noexcept = default;

	void_t HmacStream::Update(const string_view_t _chunk)
	{
		if (isFinalized) return; // 확정된 뒤의 추가 입력은 무시한다(Reset 해야 다시 시작)

		inner->Update(_chunk);
	}

	string_t HmacStream::Final() { return ne::util::Hex::Encode(FinalBytes()); }

	string_t HmacStream::FinalBytes()
	{
		if (!isFinalized)
		{
			// 내부 해시를 확정하고 그 결과에 opad 를 씌워 외부 해시를 계산한다(HMAC 정의).
			const string_t innerHex = inner->Final();
			finalized = HashBytes(type, opad + ne::util::Hex::Decode(innerHex).value_or(string_t{}));
			isFinalized = true;
		}

		return finalized;
	}

	void_t HmacStream::Reset()
	{
		inner->Init();
		inner->Update(ipad);

		ne::util::SecureWipe(finalized);
		finalized.clear();
		isFinalized = false;
	}



	// ───────────────────────── 한 줄 진입점 ─────────────────────────

	CryptoResult<string_t> Hmac(const HashType _type, const string_view_t _key, const string_view_t _message)
	{
		auto key = HMACKey::Create(_type, _key);
		if (key.IsError()) return CryptoResult<string_t>::Error(std::move(key.Error()));

		return CryptoResult<string_t>::Ok(key.Value().Generate(_message));
	}

	bool_t HmacVerify(const HashType _type, const string_view_t _key, const string_view_t _message, const string_view_t _expectedMac)
	{
		auto key = HMACKey::Create(_type, _key);
		if (key.IsError()) return false;

		return key.Value().Verify(_message, _expectedMac);
	}

	CryptoResult<string_t> HmacFile(const HashType _type, const string_view_t _key, const string_view_t _path)
	{
		auto key = HMACKey::Create(_type, _key);
		if (key.IsError()) return CryptoResult<string_t>::Error(std::move(key.Error()));

		return key.Value().GenerateFromFile(_path);
	}
}
