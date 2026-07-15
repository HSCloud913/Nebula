#pragma once
#include <vector>
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
	class SecureRandom; // Random/RandomPrime 이 참조. 정의는 Random/SecureRandom.h (.cpp 에서 include).

	/**
	 * @class BigInt
	 * @brief 임의 정밀도 부호 없는 정수(big integer)입니다.
	 *
	 * RSA 등에서 필요한 사칙연산, 비트 연산, 모듈러 연산(ModPow/ModInverse/Gcd), 소수 판별
	 * (IsProbablyPrime, 밀러-라빈)과 SecureRandom 기반 난수 생성을 제공합니다.
	 */
	class BigInt
	{
	public:
		BigInt();
		explicit BigInt(ulonglong_t _value);

		NEBULA_DEFAULT_COPY_MOVE(BigInt)

	private:
		std::vector<uint_t> d;

	public:
		bool_t operator==(const BigInt& _other) const { return d == _other.d; }
		bool_t operator!=(const BigInt& _other) const { return d != _other.d; }
		bool_t operator<(const BigInt& _other) const;
		bool_t operator<=(const BigInt& _other) const { return !(_other < *this); }
		bool_t operator>(const BigInt& _other) const { return _other < *this; }
		bool_t operator>=(const BigInt& _other) const { return !(*this < _other); }

		BigInt operator+(const BigInt& _other) const;
		BigInt operator-(const BigInt& _other) const;
		BigInt operator*(const BigInt& _other) const;
		BigInt operator/(const BigInt& _other) const { return DivMod(*this, _other).first; }
		BigInt operator%(const BigInt& _other) const { return DivMod(*this, _other).second; }
		BigInt operator>>(size_t _number) const;
		BigInt operator<<(size_t _number) const;
		BigInt operator&(const BigInt& _other) const;
		BigInt& operator+=(const BigInt& _other);
		BigInt& operator-=(const BigInt& _other);

	public:
		void_t Trim() { while (d.size() > 1 && d.back() == 0) d.pop_back(); }

		[[nodiscard]] string_t ToHex() const;
		[[nodiscard]] string_t ToBytes(size_t _minLength = 0) const; // big-endian, zero-padded to minLen

		[[nodiscard]] bool_t IsZero() const { return d.size() == 1 && d[0] == 0; }
		[[nodiscard]] bool_t IsOne() const { return d.size() == 1 && d[0] == 1; }
		[[nodiscard]] bool_t IsEven() const { return (d[0] & 1u) == 0; }
		[[nodiscard]] bool_t IsOdd() const { return (d[0] & 1u) != 0; }
		[[nodiscard]] bool_t IsProbablyPrime(int_t _rounds = 20) const;

		[[nodiscard]] size_t BitLength() const;
		[[nodiscard]] bool_t TestBit(size_t _number) const;

	public:
		static std::pair<BigInt, BigInt> DivMod(const BigInt& _lhs, const BigInt& _rhs);
		static BigInt ModPow(BigInt _base, BigInt _exp, const BigInt& _mod);
		static BigInt ModInverse(const BigInt& _lhs, const BigInt& _rhs);
		static BigInt Gcd(BigInt _lhs, BigInt _rhs);

	public:
		static BigInt FromHex(const string_t& _hex);
		static BigInt FromBytes(const string_t& _bytes); // big-endian bytes

		static BigInt Random(size_t _bits, SecureRandom& _rng);
		static BigInt RandomPrime(size_t _bits, SecureRandom& _rng);
	};

END_NS
