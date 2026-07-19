#include "Cryptography/Hash/Algorithm/SHA3.h"

#include <cstring>



// Keccak-f[1600]의 ι(iota) 스텝에서 라운드마다 state[0]에 XOR되는 라운드 상수.
// 8비트 LFSR(x^8+x^6+x^5+x^4+1)을 반복 적용해 생성되며, 라운드 간 대칭성을 깨뜨려 슬라이드/자기유사 공격을 막는 역할을 한다.
constexpr ne::ulonglong_t XorMasks[24] = { 0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
											0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
											0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
											0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL };

inline ne::ulonglong_t RotateLeft(const ne::ulonglong_t _x, const ne::byte_t _numBits) { return (_x << _numBits) | (_x >> (64 - _numBits)); }
inline ne::uint_t Mod5(const ne::uint_t _x) { return (_x < 5) ? _x : _x - 5; }



BEGIN_NS(ne::crypto)
	void_t SHA3::Init()
	{
		memset(buffer, 0, sizeof(byte_t) * MaxBlockSize);
		bufferSize = 0;
		blockSize = 200 - 2 * (static_cast<int_t>(type) / 8);
		length = 0;
	}

	void_t SHA3::AddBuffer(const void_t* _data, size_t _dataLength)
	{
		const auto* data = static_cast<const byte_t*>(_data);

		if (bufferSize > 0)
		{
			while (_dataLength > 0 && bufferSize < blockSize)
			{
				buffer[bufferSize++] = *data++;
				_dataLength--;
			}
		}

		if (bufferSize == blockSize)
		{
			ProcessBlock(buffer);
			length += blockSize;
			bufferSize = 0;
		}

		if (_dataLength == 0) return;

		while (_dataLength >= blockSize)
		{
			ProcessBlock(data);
			data += blockSize;
			length += blockSize;
			_dataLength -= blockSize;
		}

		while (_dataLength > 0)
		{
			buffer[bufferSize++] = *data++;
			_dataLength--;
		}
	}

	string_t SHA3::Get()
	{
		ProcessBuffer();

		constexpr char_t format[] = "0123456789abcdef";

		string_t result;
		result.reserve(static_cast<int_t>(type) / 4);

		const uint_t hashLength = static_cast<int_t>(type) / 64;
		for (uint_t i = 0; i < hashLength; i++)
		{
			for (uint_t j = 0; j < 8; j++) // 64 bits => 8 bytes
			{
				const auto byte = static_cast<byte_t>(sha3Value[i] >> (8 * j));
				result += format[byte >> 4];
				result += format[byte & 15];
			}
		}


		const uint_t remainder = static_cast<int_t>(type) - hashLength * 64;
		uint_t processed = 0;
		while (processed < remainder)
		{
			const auto byte = static_cast<byte_t>(sha3Value[hashLength] >> processed);
			result += format[byte >> 4];
			result += format[byte & 15];

			processed += 8;
		}

		return result;
	}



	void_t SHA3::ProcessBuffer()
	{
		size_t offset = bufferSize;
		buffer[offset++] = 0x06;

		while (offset < blockSize) { buffer[offset++] = 0; }

		buffer[offset - 1] |= 0x80;

		ProcessBlock(buffer);
	}

	void_t SHA3::ProcessBlock(const void_t* _data)
	{
		const auto data = static_cast<const ulonglong_t*>(_data);
		for (uint_t i = 0; i < blockSize / 8; i++) { sha3Value[i] ^= data[i]; }

		for (uint_t round = 0; round < 24; round++)
		{
			// Theta: 각 열(column)의 패리티를 인접 열에 XOR하여 상태 전체로 비트 변화를 확산시키는 단계.
			ulonglong_t coefficients[5];
			for (uint_t i = 0; i < 5; i++) { coefficients[i] = sha3Value[i] ^ sha3Value[i + 5] ^ sha3Value[i + 10] ^ sha3Value[i + 15] ^ sha3Value[i + 20]; }

			for (uint_t i = 0; i < 5; i++)
			{
				const ulonglong_t one = coefficients[Mod5(i + 4)] ^ RotateLeft(coefficients[Mod5(i + 1)], 1);
				sha3Value[i] ^= one;
				sha3Value[i + 5] ^= one;
				sha3Value[i + 10] ^= one;
				sha3Value[i + 15] ^= one;
				sha3Value[i + 20] ^= one;
			}

			// Rho Pi: Rho는 각 레인(lane)을 서로 다른 고정 오프셋만큼 회전시켜 비트를 레인 내부에서 재배치하고(비선형성 보조),
			// Pi는 레인들을 다른 위치로 옮겨 열/행 구조를 뒤섞어 대칭성을 깨뜨린다.
			// 아래 회전 오프셋(1,3,6,10,...)은 Keccak 스펙에 정의된 고정 테이블 값이다.
			{
				ulonglong_t last = sha3Value[1];
				ulonglong_t one = sha3Value[10];
				sha3Value[10] = RotateLeft(last, 1);
				last = one;
				one = sha3Value[7];
				sha3Value[7] = RotateLeft(last, 3);
				last = one;
				one = sha3Value[11];
				sha3Value[11] = RotateLeft(last, 6);
				last = one;
				one = sha3Value[17];
				sha3Value[17] = RotateLeft(last, 10);
				last = one;
				one = sha3Value[18];
				sha3Value[18] = RotateLeft(last, 15);
				last = one;
				one = sha3Value[3];
				sha3Value[3] = RotateLeft(last, 21);
				last = one;
				one = sha3Value[5];
				sha3Value[5] = RotateLeft(last, 28);
				last = one;
				one = sha3Value[16];
				sha3Value[16] = RotateLeft(last, 36);
				last = one;
				one = sha3Value[8];
				sha3Value[8] = RotateLeft(last, 45);
				last = one;
				one = sha3Value[21];
				sha3Value[21] = RotateLeft(last, 55);
				last = one;
				one = sha3Value[24];
				sha3Value[24] = RotateLeft(last, 2);
				last = one;
				one = sha3Value[4];
				sha3Value[4] = RotateLeft(last, 14);
				last = one;
				one = sha3Value[15];
				sha3Value[15] = RotateLeft(last, 27);
				last = one;
				one = sha3Value[23];
				sha3Value[23] = RotateLeft(last, 41);
				last = one;
				one = sha3Value[19];
				sha3Value[19] = RotateLeft(last, 56);
				last = one;
				one = sha3Value[13];
				sha3Value[13] = RotateLeft(last, 8);
				last = one;
				one = sha3Value[12];
				sha3Value[12] = RotateLeft(last, 25);
				last = one;
				one = sha3Value[2];
				sha3Value[2] = RotateLeft(last, 43);
				last = one;
				one = sha3Value[20];
				sha3Value[20] = RotateLeft(last, 62);
				last = one;
				one = sha3Value[14];
				sha3Value[14] = RotateLeft(last, 18);
				last = one;
				one = sha3Value[22];
				sha3Value[22] = RotateLeft(last, 39);
				last = one;
				one = sha3Value[9];
				sha3Value[9] = RotateLeft(last, 61);
				last = one;
				one = sha3Value[6];
				sha3Value[6] = RotateLeft(last, 20);
				last = one;
				sha3Value[1] = RotateLeft(last, 44);
			}

			// Chi: 같은 행(row) 내에서 비선형 조합(x ^ (~y & z))을 적용하는 유일한 비선형 단계 — 해시의 안전성(역상 저항성)의 핵심.
			for (uint_t j = 0; j < 25; j += 5)
			{
				const ulonglong_t one = sha3Value[j];
				const ulonglong_t two = sha3Value[j + 1];

				sha3Value[j] ^= sha3Value[j + 2] & ~two;
				sha3Value[j + 1] ^= sha3Value[j + 3] & ~sha3Value[j + 2];
				sha3Value[j + 2] ^= sha3Value[j + 4] & ~sha3Value[j + 3];
				sha3Value[j + 3] ^= one & ~sha3Value[j + 4];
				sha3Value[j + 4] ^= two & ~one;
			}

			// Iota: 라운드 상수를 state[0]에만 XOR하여 라운드마다 상태를 비대칭적으로 만든다(라운드 함수 자체의 대칭성 제거).
			sha3Value[0] ^= XorMasks[round];
		}
	}

END_NS
