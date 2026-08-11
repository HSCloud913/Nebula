//
// Created by hscloud on 26. 8. 6.
//

#include "Cryptography/Kdf.h"

#include <utility>
#include "Cryptography/HMAC.h"
#include "Util/SecureWipe.h"



namespace ne::crypto
{
	namespace
	{
		// HMAC 출력 길이(= 해시 출력 길이)를 빈 메시지 한 번으로 알아낸다 — 해시별 길이 표를 따로
		// 유지하지 않아도 되고 알고리즘이 추가돼도 자동으로 맞는다.
		std::size_t MacLength(const HMACKey& _key) { return _key.GenerateBytes("").size(); }

		string_t EncodeBigEndian32(const uint_t _value)
		{
			string_t out;
			out += static_cast<char_t>((_value >> 24) & 0xFF);
			out += static_cast<char_t>((_value >> 16) & 0xFF);
			out += static_cast<char_t>((_value >> 8) & 0xFF);
			out += static_cast<char_t>(_value & 0xFF);

			return out;
		}
	}



	CryptoResult<string_t> HkdfExtract(const HashType _hashType, const string_view_t _inputKey, const string_view_t _salt)
	{
		using R = CryptoResult<string_t>;

		// salt 를 HMAC 키로, IKM 을 메시지로 쓴다. salt 가 비면 RFC 는 HashLen 만큼의 0 을 쓰라고 하는데,
		// HMAC 의 키 정규화가 어차피 블록 크기까지 0 으로 패딩하므로 빈 키와 결과가 같다.
		auto key = HMACKey::Create(_hashType, _salt);
		if (key.IsError()) return R::Error(std::move(key.Error()).Context("[HkdfExtract]"));

		return R::Ok(key.Value().GenerateBytes(_inputKey));
	}

	CryptoResult<string_t> HkdfExpand(const HashType _hashType, const string_view_t _pseudoRandomKey, const string_view_t _info, const std::size_t _length)
	{
		using R = CryptoResult<string_t>;

		if (_length == 0) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "requested key length must be positive" });

		auto key = HMACKey::Create(_hashType, _pseudoRandomKey);
		if (key.IsError()) return R::Error(std::move(key.Error()).Context("[HkdfExpand]"));

		const std::size_t macLength = MacLength(key.Value());
		if (macLength == 0) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "hash produced no output" });
		if (_length > 255 * macLength) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "HKDF output limited to 255 hash lengths" });

		// T(i) = HMAC(PRK, T(i-1) || info || i), OKM = T(1) || T(2) || ... 의 앞 _length 바이트.
		string_t output;
		output.reserve(_length);

		string_t previous;
		for (uint_t counter = 1; output.size() < _length; ++counter)
		{
			string_t input = previous;
			input.append(_info);
			input += static_cast<char_t>(counter & 0xFF);

			previous = key.Value().GenerateBytes(input);
			output += previous;
		}

		ne::util::SecureWipe(previous);
		output.resize(_length);

		return R::Ok(std::move(output));
	}

	CryptoResult<string_t> Hkdf(const HashType _hashType, const string_view_t _inputKey, const string_view_t _salt, const string_view_t _info, const std::size_t _length)
	{
		using R = CryptoResult<string_t>;

		auto prk = HkdfExtract(_hashType, _inputKey, _salt);
		if (prk.IsError()) return R::Error(std::move(prk.Error()));

		auto output = HkdfExpand(_hashType, prk.Value(), _info, _length);
		ne::util::SecureWipe(prk.Value());

		return output;
	}



	CryptoResult<string_t> Pbkdf2(const HashType _hashType, const string_view_t _password, const string_view_t _salt, const uint_t _iterations, const std::size_t _length)
	{
		using R = CryptoResult<string_t>;

		if (_iterations == 0) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "iteration count must be positive" });
		if (_length == 0) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "requested key length must be positive" });

		auto key = HMACKey::Create(_hashType, _password);
		if (key.IsError()) return R::Error(std::move(key.Error()).Context("[Pbkdf2]"));

		const std::size_t macLength = MacLength(key.Value());
		if (macLength == 0) return R::Error(CryptoError{ CryptoErrorKind::INVALID_INPUT, "hash produced no output" });

		string_t output;
		output.reserve(_length);

		// T(i) = U(1) ^ U(2) ^ ... ^ U(c), U(1) = HMAC(P, salt || INT32BE(i)), U(j) = HMAC(P, U(j-1))
		for (uint_t block = 1; output.size() < _length; ++block)
		{
			string_t previous = key.Value().GenerateBytes(string_t(_salt).append(EncodeBigEndian32(block)));
			string_t accumulated = previous;

			for (uint_t iteration = 1; iteration < _iterations; ++iteration)
			{
				previous = key.Value().GenerateBytes(previous);
				for (std::size_t i = 0; i < macLength; ++i) accumulated[i] = static_cast<char_t>(accumulated[i] ^ previous[i]);
			}

			output += accumulated;

			ne::util::SecureWipe(previous);
			ne::util::SecureWipe(accumulated);
		}

		output.resize(_length);

		return R::Ok(std::move(output));
	}
}
