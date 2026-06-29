#include "BigInt.h"

#include <algorithm>
#include <stdexcept>



BEGIN_NS(ne::cryptography)
	BigInt::BigInt()
		: d(1, 0u) {}

	BigInt::BigInt(const ulonglong_t _value)
	{
		d.push_back(static_cast<uint_t>(_value & 0xFFFFFFFFu));
		d.push_back(static_cast<uint_t>(_value >> 32));

		Trim();
	}



	bool BigInt::operator<(const BigInt& _other) const
	{
		if (d.size() != _other.d.size()) return d.size() < _other.d.size();

		for (int_t i = static_cast<int_t>(d.size()) - 1; i >= 0; --i)
		{
			if (d[i] != _other.d[i]) return d[i] < _other.d[i];
		}

		return false;
	}


	BigInt BigInt::operator+(const BigInt& _other) const
	{
		BigInt result;
		result.d.resize(std::max(d.size(), _other.d.size()) + 1, 0u);

		ulonglong_t carry = 0;
		for (size_t i = 0; i < result.d.size(); ++i)
		{
			const ulonglong_t a = i < d.size() ? d[i] : 0u;
			const ulonglong_t b = i < _other.d.size() ? _other.d[i] : 0u;
			const ulonglong_t s = a + b + carry;
			result.d[i] = static_cast<uint_t>(s & 0xFFFFFFFFu);
			carry = s >> 32;
		}

		result.Trim();

		return result;
	}

	BigInt& BigInt::operator+=(const BigInt& _other)
	{
		*this = *this + _other;
		return *this;
	}

	BigInt BigInt::operator-(const BigInt& _other) const
	{
		BigInt result;
		result.d.resize(d.size(), 0u);

		longlong_t borrow = 0;
		for (size_t i = 0; i < d.size(); ++i)
		{
			const longlong_t a = d[i];
			const longlong_t b = i < _other.d.size() ? static_cast<longlong_t>(_other.d[i]) : 0;

			longlong_t diff = a - b - borrow;
			if (diff < 0)
			{
				diff += (static_cast<longlong_t>(1) << 32);
				borrow = 1;
			}
			else
			{
				borrow = 0;
			}

			result.d[i] = static_cast<uint_t>(diff);
		}

		result.Trim();

		return result;
	}

	BigInt& BigInt::operator-=(const BigInt& _other)
	{
		*this = *this - _other;
		return *this;
	}

	BigInt BigInt::operator*(const BigInt& _other) const
	{
		BigInt result;
		result.d.assign(d.size() + _other.d.size(), 0u);
		for (size_t i = 0; i < d.size(); ++i)
		{
			ulonglong_t carry = 0;
			for (size_t j = 0; j < _other.d.size(); ++j)
			{
				const ulonglong_t cur = static_cast<ulonglong_t>(d[i]) * _other.d[j]
										+ result.d[i + j] + carry;
				result.d[i + j] = static_cast<uint_t>(cur & 0xFFFFFFFFu);
				carry = cur >> 32;
			}
			result.d[i + _other.d.size()] += static_cast<uint_t>(carry);
		}
		result.Trim();
		return result;
	}

	BigInt BigInt::operator>>(const size_t _number) const
	{
		if (_number == 0) return *this;

		const size_t wordShift = _number / 32;
		const size_t bitShift = _number % 32;
		if (wordShift >= d.size()) return {};

		BigInt result;
		result.d.resize(d.size() - wordShift, 0u);

		for (size_t i = 0; i < result.d.size(); ++i)
		{
			result.d[i] = bitShift ? (d[i + wordShift] >> bitShift) : d[i + wordShift];
			if (bitShift > 0 && i + wordShift + 1 < d.size()) result.d[i] |= d[i + wordShift + 1] << (32 - bitShift);
		}

		result.Trim();

		return result;
	}

	BigInt BigInt::operator<<(const size_t _number) const
	{
		if (_number == 0) return *this;

		const size_t wordShift = _number / 32;
		const size_t bitShift = _number % 32;

		BigInt result;
		result.d.assign(d.size() + wordShift + 1, 0u);

		for (size_t i = 0; i < d.size(); ++i)
		{
			result.d[i + wordShift] |= bitShift ? (d[i] << bitShift) : d[i];
			if (bitShift > 0 && i + wordShift + 1 < result.d.size()) result.d[i + wordShift + 1] |= d[i] >> (32 - bitShift);
		}

		result.Trim();

		return result;
	}

	BigInt BigInt::operator&(const BigInt& _other) const
	{
		BigInt result;
		result.d.resize(std::min(d.size(), _other.d.size()), 0u);

		for (size_t i = 0; i < result.d.size(); ++i)
		{
			result.d[i] = d[i] & _other.d[i];
		}

		result.Trim();

		return result;
	}



	void BigInt::Trim()
	{
		while (d.size() > 1 && d.back() == 0) d.pop_back();
	}


	string_t BigInt::ToHex() const
	{
		static constexpr char_t hexChars[] = "0123456789abcdef";
		string_t result;

		for (int_t i = static_cast<int_t>(d.size()) - 1; i >= 0; --i)
		{
			uint_t w = d[i];
			for (int_t shift = 28; shift >= 0; shift -= 4) result += hexChars[(w >> shift) & 0xFu];
		}

		const size_t nonZero = result.find_first_not_of('0');

		return nonZero == string_t::npos ? "0" : result.substr(nonZero);
	}

	string_t BigInt::ToBytes(const size_t _minLength) const
	{
		string_t result;
		for (int_t i = static_cast<int_t>(d.size()) - 1; i >= 0; --i)
		{
			uint_t w = d[i];
			result += static_cast<char_t>((w >> 24) & 0xFFu);
			result += static_cast<char_t>((w >> 16) & 0xFFu);
			result += static_cast<char_t>((w >> 8) & 0xFFu);
			result += static_cast<char_t>(w & 0xFFu);
		}

		size_t nonZero = 0;
		while (nonZero < result.size() - 1 && result[nonZero] == 0) ++nonZero;
		result = result.substr(nonZero);

		if (result.size() < _minLength) result.insert(0, _minLength - result.size(), '\0');

		return result;
	}


	bool BigInt::IsProbablyPrime(int_t rounds) const
	{
		if (*this <= BigInt(1u)) return false;
		if (*this == BigInt(2u) || *this == BigInt(3u)) return true;
		if (IsEven()) return false;

		BigInt n1 = *this - BigInt(1u);
		size_t r = 0;
		BigInt dd = n1;
		while (dd.IsEven())
		{
			dd = dd >> 1;
			++r;
		}

		std::mt19937_64 rng(12345678901234567ULL);

		for (int_t i = 0; i < rounds; ++i)
		{
			BigInt a = Random(BitLength(), rng);
			a = a % (*this - BigInt(3u)) + BigInt(2u);

			BigInt x = ModPow(a, dd, *this);
			if (x.IsOne() || x == n1) continue;

			bool composite = true;
			for (size_t j = 0; j < r - 1; ++j)
			{
				x = x * x % *this;
				if (x == n1)
				{
					composite = false;
					break;
				}
			}
			if (composite) return false;
		}
		return true;
	}


	size_t BigInt::BitLength() const
	{
		if (IsZero()) return 0;
		size_t bits = (d.size() - 1) * 32;
		uint_t top = d.back();
		while (top)
		{
			++bits;
			top >>= 1;
		}
		return bits;
	}

	bool BigInt::TestBit(const size_t _number) const
	{
		const size_t word = _number / 32;
		const size_t bit = _number % 32;
		return word < d.size() && ((d[word] >> bit) & 1u);
	}



	std::pair<BigInt, BigInt> BigInt::DivMod(const BigInt& _lhs, const BigInt& _rhs)
	{
		if (_rhs.IsZero()) throw std::domain_error("BigInt division by zero");
		if (_lhs < _rhs) return std::make_pair(BigInt(), _lhs);

		BigInt q, r;
		for (int_t i = static_cast<int_t>(_lhs.BitLength()) - 1; i >= 0; --i)
		{
			r = r << 1;
			if (_lhs.TestBit(static_cast<size_t>(i)))
			{
				if (r.d.empty()) r.d.push_back(1u);
				else r.d[0] |= 1u;
			}
			if (r >= _rhs)
			{
				r -= _rhs;
				const size_t qi = static_cast<size_t>(i);
				const size_t qw = qi / 32;
				const size_t qb = qi % 32;
				if (q.d.size() <= qw) q.d.resize(qw + 1, 0u);
				q.d[qw] |= (1u << qb);
			}
		}

		q.Trim();
		r.Trim();
		return std::make_pair(std::move(q), std::move(r));
	}

	BigInt BigInt::ModPow(BigInt _base, BigInt _exp, const BigInt& _mod)
	{
		BigInt result(1u);

		_base = _base % _mod;
		while (!_exp.IsZero())
		{
			if (_exp.IsOdd()) result = result * _base % _mod;
			_exp = _exp >> 1;
			_base = _base * _base % _mod;
		}

		return result;
	}

	BigInt BigInt::ModInverse(const BigInt& _lhs, const BigInt& _rhs)
	{
		BigInt old_r = _lhs, r = _rhs;
		BigInt old_s(1u), s;
		bool old_s_neg = false, s_neg = true;

		while (!r.IsZero())
		{
			auto [q, rem] = DivMod(old_r, r);

			BigInt new_r = std::move(rem);
			old_r = std::move(r);
			r = std::move(new_r);

			BigInt qs = q * s;
			BigInt new_s;
			bool new_s_neg;
			if (old_s_neg == s_neg)
			{
				if (old_s >= qs)
				{
					new_s = old_s - qs;
					new_s_neg = old_s_neg;
				}
				else
				{
					new_s = qs - old_s;
					new_s_neg = !old_s_neg;
				}
			}
			else
			{
				new_s = old_s + qs;
				new_s_neg = old_s_neg;
			}

			old_s = std::move(s);
			old_s_neg = s_neg;
			s = std::move(new_s);
			s_neg = new_s_neg;
		}

		return old_s_neg ? _rhs - old_s % _rhs : old_s % _rhs;
	}

	BigInt BigInt::Gcd(BigInt _lhs, BigInt _rhs)
	{
		while (!_rhs.IsZero())
		{
			BigInt temp = _lhs % _rhs;
			_lhs = std::move(_rhs);
			_rhs = std::move(temp);
		}

		return _lhs;
	}



	BigInt BigInt::FromHex(const string_t& _hex)
	{
		BigInt result;
		result.d.clear();

		auto fromHexChar = [](char_t c) -> uint_t
		{
			if (c >= '0' && c <= '9') return static_cast<uint_t>(c - '0');
			if (c >= 'a' && c <= 'f') return static_cast<uint_t>(c - 'a' + 10);
			return static_cast<uint_t>(c - 'A' + 10);
		};

		size_t start = 0;
		if (_hex.size() >= 2 && _hex[0] == '0' && (_hex[1] == 'x' || _hex[1] == 'X')) start = 2;

		int_t i = static_cast<int_t>(_hex.size()) - 1;
		while (i >= static_cast<int_t>(start))
		{
			uint_t word = 0;
			for (int_t shift = 0; shift < 32 && i >= static_cast<int_t>(start); shift += 4, --i) word |= fromHexChar(_hex[i]) << shift;
			result.d.push_back(word);
		}

		result.Trim();

		return result;
	}

	BigInt BigInt::FromBytes(const string_t& _bytes)
	{
		BigInt result;
		result.d.clear();

		int_t i = static_cast<int_t>(_bytes.size()) - 1;
		while (i >= 0)
		{
			uint_t word = 0;
			for (int_t shift = 0; shift < 32 && i >= 0; shift += 8, --i) word |= static_cast<uint_t>(static_cast<byte_t>(_bytes[i])) << shift;
			result.d.push_back(word);
		}

		result.Trim();

		return result;
	}



	BigInt BigInt::Random(const size_t _bits, std::mt19937_64& _rng)
	{
		const size_t words = (_bits + 31) / 32;

		BigInt result;
		result.d.resize(words, 0u);

		for (auto& w : result.d) w = static_cast<uint_t>(_rng());

		const size_t topBits = _bits % 32;
		if (topBits > 0) result.d.back() &= (1u << topBits) - 1u;

		result.Trim();

		return result;
	}

	BigInt BigInt::RandomPrime(const size_t _bits, std::mt19937_64& _rng)
	{
		while (true)
		{
			const size_t topWord = (_bits - 1) / 32;
			const size_t topBit = (_bits - 1) % 32;

			BigInt candidate = Random(_bits, _rng);
			if (candidate.d.size() <= topWord) candidate.d.resize(topWord + 1, 0u);

			candidate.d[topWord] |= (1u << topBit);
			candidate.d[0] |= 1u;
			candidate.Trim();

			const int_t rounds = _bits >= 1024 ? 4 : (_bits >= 512 ? 5 : 10);
			if (candidate.IsProbablyPrime(rounds)) return candidate;
		}
	}

END_NS
