#include "Cryptography/Hash/Algorithm/SHA2.h"

#include <cstring>
#include <iomanip>
#include <sstream>



constexpr ne::uint_t Sha256RoundConstants[64] = { 0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
												0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
												0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
												0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
												0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
												0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
												0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL };

constexpr ne::ulonglong_t Sha512RoundConstants[80] = { 0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
														0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
														0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
														0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
														0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
														0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
														0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
														0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
														0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
														0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
														0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
														0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
														0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
														0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL };

inline ne::uint_t Min(const ne::uint_t _x, const ne::uint_t _y) { return _x < _y ? _x : _y; }

inline ne::uint_t Load32(const ne::byte_t* _y)
{
	return (static_cast<ne::uint_t>(_y[0]) << 24) | (static_cast<ne::uint_t>(_y[1]) << 16) | (static_cast<ne::uint_t>(_y[2]) << 8) | (static_cast<ne::uint_t>(_y[3]) << 0);
}

inline ne::ulonglong_t Load64(const ne::byte_t* _y)
{
	ne::ulonglong_t result = 0;
	for (int i = 0; i != 8; ++i) result |= static_cast<ne::ulonglong_t>(_y[i]) << ((7 - i) * 8);

	return result;
}

inline void Store32(const ne::uint_t _x, ne::byte_t* _y) { for (int i = 0; i != 4; ++i) _y[i] = (_x >> ((3 - i) * 8)) & 255; }

inline void Store64(const ne::ulonglong_t _x, ne::byte_t* _y) { for (int i = 0; i != 8; ++i) _y[i] = (_x >> ((7 - i) * 8)) & 255; }

inline ne::uint_t Ch(const ne::uint_t _x, const ne::uint_t _y, const ne::uint_t _z) { return _z ^ (_x & (_y ^ _z)); }

inline ne::ulonglong_t Ch(const ne::ulonglong_t _x, const ne::ulonglong_t _y, const ne::ulonglong_t _z) { return _z ^ (_x & (_y ^ _z)); }

inline ne::uint_t Maj(const ne::uint_t _x, const ne::uint_t _y, const ne::uint_t _z) { return ((_x | _y) & _z) | (_x & _y); }

inline ne::ulonglong_t Maj(const ne::ulonglong_t _x, const ne::ulonglong_t _y, const ne::ulonglong_t _z) { return ((_x | _y) & _z) | (_x & _y); }

inline ne::uint_t Rot(const ne::uint_t _x, const ne::uint_t _n) { return (_x >> (_n & 31)) | (_x << (32 - (_n & 31))); }

inline ne::ulonglong_t Rot(const ne::ulonglong_t _x, const ne::ulonglong_t _n) { return (_x >> (_n & 63)) | (_x << (64 - (_n & 63))); }

inline ne::uint_t Sh(const ne::uint_t _x, const ne::uint_t _n) { return _x >> _n; }

inline ne::ulonglong_t Sh(const ne::ulonglong_t _x, const ne::ulonglong_t _n) { return _x >> _n; }

// Sigma0/Sigma1(대문자, 압축함수용)과 Gamma0/Gamma1(메시지 스케줄용)의 회전/시프트 상수는
// FIPS 180-4 스펙에 정의된 고정값이다. SHA-256(32비트): Sigma0=2,13,22 / Sigma1=6,11,25 / Gamma0=7,18,>>3 / Gamma1=17,19,>>10.
// SHA-512(64비트)는 워드 크기가 다르므로 별도 상수(28,34,39 / 14,18,41 / 1,8,>>7 / 19,61,>>6)를 사용한다.
inline ne::uint_t Sigma0(const ne::uint_t _x) { return Rot(_x, 2) ^ Rot(_x, 13) ^ Rot(_x, 22); }

inline ne::ulonglong_t Sigma0(const ne::ulonglong_t _x) { return Rot(_x, 28) ^ Rot(_x, 34) ^ Rot(_x, 39); }

inline ne::uint_t Sigma1(const ne::uint_t _x) { return Rot(_x, 6) ^ Rot(_x, 11) ^ Rot(_x, 25); }

inline ne::ulonglong_t Sigma1(const ne::ulonglong_t _x) { return Rot(_x, 14) ^ Rot(_x, 18) ^ Rot(_x, 41); }

inline ne::uint_t Gamma0(const ne::uint_t _x) { return Rot(_x, 7) ^ Rot(_x, 18) ^ Sh(_x, 3); }

inline ne::ulonglong_t Gamma0(const ne::ulonglong_t _x) { return Rot(_x, 1) ^ Rot(_x, 8) ^ Sh(_x, 7); }

inline ne::uint_t Gamma1(const ne::uint_t _x) { return Rot(_x, 17) ^ Rot(_x, 19) ^ Sh(_x, 10); }

inline ne::ulonglong_t Gamma1(const ne::ulonglong_t _x) { return Rot(_x, 19) ^ Rot(_x, 61) ^ Sh(_x, 6); }

inline void ShaCompress(ne::uint_t* _state, const ne::byte_t* _buffer)
{
	ne::uint_t S[8], W[64], t0, t1, t;

	for (int i = 0; i < 8; i++) S[i] = _state[i];

	for (int i = 0; i < 16; i++) W[i] = Load32(_buffer + (4 * i));

	for (int i = 16; i < 64; i++) W[i] = Gamma1(W[i - 2]) + W[i - 7] + Gamma0(W[i - 15]) + W[i - 16];

	auto RND = [&](ne::uint_t _a, ne::uint_t _b, ne::uint_t _c, ne::uint_t& _d, ne::uint_t _e, ne::uint_t _f, ne::uint_t _g, ne::uint_t& _h, ne::uint_t _i)
	{
		t0 = _h + Sigma1(_e) + Ch(_e, _f, _g) + Sha256RoundConstants[_i] + W[_i];
		t1 = Sigma0(_a) + Maj(_a, _b, _c);
		_d += t0;
		_h = t0 + t1;
	};

	for (auto i = 0; i < 64; ++i)
	{
		RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], i);
		t = S[7];
		S[7] = S[6];
		S[6] = S[5];
		S[5] = S[4];
		S[4] = S[3];
		S[3] = S[2];
		S[2] = S[1];
		S[1] = S[0];
		S[0] = t;
	}

	for (int i = 0; i < 8; i++) _state[i] = _state[i] + S[i];
}

inline void ShaCompress(ne::ulonglong_t* _state, const ne::byte_t* _buffer)
{
	ne::ulonglong_t S[8], W[80], t0, t1;

	for (int i = 0; i < 8; i++) S[i] = _state[i];

	for (int i = 0; i < 16; i++) W[i] = Load64(_buffer + (8 * i));

	for (int i = 16; i < 80; i++) W[i] = Gamma1(W[i - 2]) + W[i - 7] + Gamma0(W[i - 15]) + W[i - 16];

	auto RND = [&](ne::ulonglong_t _a, ne::ulonglong_t _b, ne::ulonglong_t _c, ne::ulonglong_t& _d, ne::ulonglong_t _e, ne::ulonglong_t _f, ne::ulonglong_t _g, ne::ulonglong_t& _h, ne::ulonglong_t _i)
	{
		t0 = _h + Sigma1(_e) + Ch(_e, _f, _g) + Sha512RoundConstants[_i] + W[_i];
		t1 = Sigma0(_a) + Maj(_a, _b, _c);
		_d += t0;
		_h = t0 + t1;
	};

	// SHA-256과 달리 상태를 물리적으로 회전(S[7]=S[6]... 재대입)시키지 않고,
	// 8라운드를 한 그룹으로 풀어서(unroll) 매 호출마다 8개 상태 워드(a~h)의 인자 위치를 한 칸씩 밀어 전달한다.
	// 8라운드가 지나면 원래 위치로 되돌아오므로 결과는 동일하되 배열 복사 비용이 없어 더 빠르다.
	for (int i = 0; i < 80; i += 8)
	{
		RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], i + 0);
		RND(S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], i + 1);
		RND(S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], i + 2);
		RND(S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], i + 3);
		RND(S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], i + 4);
		RND(S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], i + 5);
		RND(S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], i + 6);
		RND(S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], i + 7);
	}

	for (int i = 0; i < 8; i++) _state[i] = _state[i] + S[i];
}



BEGIN_NS(ne::crypto)
	void_t SHA2::Init()
	{
		length = 0;
		currentLength = 0;

		if (type == Type::SHA2_224)
		{
			sha2Value32[0] = 0xc1059ed8UL;
			sha2Value32[1] = 0x367cd507UL;
			sha2Value32[2] = 0x3070dd17UL;
			sha2Value32[3] = 0xf70e5939UL;
			sha2Value32[4] = 0xffc00b31UL;
			sha2Value32[5] = 0x68581511UL;
			sha2Value32[6] = 0x64f98fa7UL;
			sha2Value32[7] = 0xbefa4fa4UL;
		}
		else if (type == Type::SHA2_256)
		{
			sha2Value32[0] = 0x6A09E667UL;
			sha2Value32[1] = 0xBB67AE85UL;
			sha2Value32[2] = 0x3C6EF372UL;
			sha2Value32[3] = 0xA54FF53AUL;
			sha2Value32[4] = 0x510E527FUL;
			sha2Value32[5] = 0x9B05688CUL;
			sha2Value32[6] = 0x1F83D9ABUL;
			sha2Value32[7] = 0x5BE0CD19UL;
		}
		else if (type == Type::SHA2_384)
		{
			sha2Value64[0] = 0xcbbb9d5dc1059ed8ULL;
			sha2Value64[1] = 0x629a292a367cd507ULL;
			sha2Value64[2] = 0x9159015a3070dd17ULL;
			sha2Value64[3] = 0x152fecd8f70e5939ULL;
			sha2Value64[4] = 0x67332667ffc00b31ULL;
			sha2Value64[5] = 0x8eb44a8768581511ULL;
			sha2Value64[6] = 0xdb0c2e0d64f98fa7ULL;
			sha2Value64[7] = 0x47b5481dbefa4fa4ULL;
		}
		else if (type == Type::SHA2_512)
		{
			sha2Value64[0] = 0x6a09e667f3bcc908ULL;
			sha2Value64[1] = 0xbb67ae8584caa73bULL;
			sha2Value64[2] = 0x3c6ef372fe94f82bULL;
			sha2Value64[3] = 0xa54ff53a5f1d36f1ULL;
			sha2Value64[4] = 0x510e527fade682d1ULL;
			sha2Value64[5] = 0x9b05688c2b3e6c1fULL;
			sha2Value64[6] = 0x1f83d9abfb41bd6bULL;
			sha2Value64[7] = 0x5be0cd19137e2179ULL;
		}
	}

	void_t SHA2::AddBuffer(const void_t* _data, size_t _dataLength)
	{
		if (type == Type::SHA2_224 || type == Type::SHA2_256)
		{
			auto data = static_cast<const byte_t*>(_data);

			while (_dataLength > 0)
			{
				if (constexpr size_t BlockSize = 64; currentLength == 0 && _dataLength >= BlockSize)
				{
					ShaCompress(sha2Value32, data);
					length += BlockSize * 8;
					data += BlockSize;
					_dataLength -= BlockSize;
				}
				else
				{
					const uint_t n = Min(_dataLength, (BlockSize - currentLength));
					std::memcpy(buffer + currentLength, data, n);
					currentLength += n;
					data += n;
					_dataLength -= n;

					if (currentLength == BlockSize)
					{
						ShaCompress(sha2Value32, buffer);
						length += 8 * BlockSize;
						currentLength = 0;
					}
				}
			}
		}
		else if (type == Type::SHA2_384 || type == Type::SHA2_512)
		{
			auto data = static_cast<const byte_t*>(_data);

			while (_dataLength > 0)
			{
				if (constexpr ne::uint_t BlockSize = 128; currentLength == 0 && _dataLength >= BlockSize)
				{
					ShaCompress(sha2Value64, data);
					length += BlockSize * 8;
					data += BlockSize;
					_dataLength -= BlockSize;
				}
				else
				{
					const uint_t n = Min(_dataLength, (BlockSize - currentLength));
					std::memcpy(buffer + currentLength, data, n);
					currentLength += n;
					data += n;
					_dataLength -= n;

					if (currentLength == BlockSize)
					{
						ShaCompress(sha2Value64, buffer);
						length += 8 * BlockSize;
						currentLength = 0;
					}
				}
			}
		}
	}

	string_t SHA2::Get()
	{
		std::stringstream ss;
		byte_t result[64];

		if (type == Type::SHA2_224 || type == Type::SHA2_256)
		{
			length += currentLength * 8;

			// Append the '1' bit
			buffer[currentLength++] = static_cast<byte_t>(0x80);

			// If the length is currently above 56 bytes we append zeros then compress.
			// Then we can fall back to padding zeros and length encoding like normal.
			if (currentLength > 56)
			{
				while (currentLength < 64) { buffer[currentLength++] = 0; }

				ShaCompress(sha2Value32, buffer);
				currentLength = 0;
			}

			while (currentLength < 56) { buffer[currentLength++] = 0; }

			Store64(length, buffer + 56);
			ShaCompress(sha2Value32, buffer);

			for (int i = 0; i < 8; i++) Store32(sha2Value32[i], result + (4 * i));
		}
		else if (type == Type::SHA2_384 || type == Type::SHA2_512)
		{
			length += currentLength * 8ULL;

			// Append the '1' bit
			buffer[currentLength++] = static_cast<byte_t>(0x80);

			// If the length is currently above 112 bytes we append zeros then compress.
			// Then we can fall back to padding zeros and length encoding like normal.
			if (currentLength > 112)
			{
				while (currentLength < 128) buffer[currentLength++] = 0;
				ShaCompress(sha2Value64, buffer);
				currentLength = 0;
			}

			// note: that from 112 to 120 is the 64 MSB of the length.  We assume that
			// you won't hash 2^64 bits of data... :-)
			while (currentLength < 120) buffer[currentLength++] = 0;

			Store64(length, buffer + 120);
			ShaCompress(sha2Value64, buffer);

			for (int i = 0; i < 8; i++) Store64(sha2Value64[i], result + (8 * i));
		}

		for (int i = 0; i != static_cast<int_t>(type) / 8; ++i) { ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int_t>(result[i]); }

		return ss.str();
	}

END_NS
