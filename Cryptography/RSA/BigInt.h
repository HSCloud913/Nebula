#ifndef NEBULA_BIGINT_H
#define NEBULA_BIGINT_H

#include "Type.h"

#include <vector>
#include <random>

BEGIN_NS(ne::cryptography)
	class BigInt
	{
	public:
		NEBULA_DEFAULT_COPY(BigInt)
		NEBULA_DEFAULT_MOVE(BigInt)

	public:
		BigInt();
		explicit BigInt(ulonglong_t _value);

	private:
		std::vector<uint_t> d;

	public:
		bool operator==(const BigInt& _other) const { return d == _other.d; }
		bool operator!=(const BigInt& _other) const { return d != _other.d; }
		bool operator<(const BigInt& _other) const;
		bool operator<=(const BigInt& _other) const { return !(_other < *this); }
		bool operator>(const BigInt& _other) const { return _other < *this; }
		bool operator>=(const BigInt& _other) const { return !(*this < _other); }

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
		void Trim();

		[[nodiscard]] string_t ToHex() const;
		[[nodiscard]] string_t ToBytes(size_t _minLength = 0) const; // big-endian, zero-padded to minLen

		[[nodiscard]] bool IsZero() const { return d.size() == 1 && d[0] == 0; }
		[[nodiscard]] bool IsOne() const { return d.size() == 1 && d[0] == 1; }
		[[nodiscard]] bool IsEven() const { return (d[0] & 1u) == 0; }
		[[nodiscard]] bool IsOdd() const { return (d[0] & 1u) != 0; }
		[[nodiscard]] bool IsProbablyPrime(int_t _rounds = 20) const;

		[[nodiscard]] size_t BitLength() const;
		[[nodiscard]] bool TestBit(size_t _number) const;

	public:
		static std::pair<BigInt, BigInt> DivMod(const BigInt& _lhs, const BigInt& _rhs);
		static BigInt ModPow(BigInt _base, BigInt _exp, const BigInt& _mod);
		static BigInt ModInverse(const BigInt& _lhs, const BigInt& _rhs);
		static BigInt Gcd(BigInt _lhs, BigInt _rhs);

	public:
		static BigInt FromHex(const string_t& _hex);
		static BigInt FromBytes(const string_t& _bytes); // big-endian bytes

		static BigInt Random(size_t _bits, std::mt19937_64& _rng);
		static BigInt RandomPrime(size_t _bits, std::mt19937_64& _rng);
	};

END_NS

#endif //NEBULA_BIGINT_H
