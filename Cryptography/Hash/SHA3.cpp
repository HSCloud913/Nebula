#include "SHA3.h"

#include <cstring>



constexpr ne::ulonglong_t XorMasks[] =
{
	0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
	0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
	0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
	0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
	0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
	0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
	0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
	0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};



inline ne::ulonglong_t RotateLeft(ne::ulonglong_t x, ne::byte_t _numBits)
{
	return (x << _numBits) | (x >> (64 - _numBits));
}
inline ne::ulonglong_t Swap(ne::ulonglong_t x)
{
	return (x >> 56) |
			((x >> 40) & 0x000000000000FF00ULL) |
			((x >> 24) & 0x0000000000FF0000ULL) |
			((x >> 8) & 0x00000000FF000000ULL) |
			((x << 8) & 0x000000FF00000000ULL) |
			((x << 24) & 0x0000FF0000000000ULL) |
			((x << 40) & 0x00FF000000000000ULL) |
			(x << 56);
}
inline ne::uint_t Mod5(ne::uint_t x)
{
	return (x < 5) ? x : x - 5;
}



BEGIN_NS(ne::cryptography)
	void SHA3::Init()
	{
		blockSize = 200 - 2 * (static_cast<int_t>(type) / 8);
		memset(buffer, 0, sizeof(byte_t) * MaxBlockSize);
	}

	void SHA3::AddBuffer(const void_t* _data, size_t _dataLength)
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



	void SHA3::ProcessBuffer()
	{
		size_t offset = bufferSize;
		buffer[offset++] = 0x06;

		while (offset < blockSize)
		{
			buffer[offset++] = 0;
		}

		buffer[offset - 1] |= 0x80;

		ProcessBlock(buffer);
	}

	void SHA3::ProcessBlock(const void_t* _data)
	{
		auto data = static_cast<const ulonglong_t*>(_data);
		for (uint_t i = 0; i < blockSize / 8; i++)
		{
#if defined(__BYTE_ORDER) && (__BYTE_ORDER != 0) && (__BYTE_ORDER == __BIG_ENDIAN)
			sha3Value[i] ^= swap(data[i]);
#else
			sha3Value[i] ^= data[i];
#endif
		}

		for (uint_t round = 0; round < 24; round++)
		{
			ulonglong_t coefficients[5];
			for (uint_t i = 0; i < 5; i++)
			{
				coefficients[i] = sha3Value[i] ^ sha3Value[i + 5] ^ sha3Value[i + 10] ^ sha3Value[i + 15] ^ sha3Value[i + 20];
			}

			for (uint_t i = 0; i < 5; i++)
			{
				const ulonglong_t one = coefficients[Mod5(i + 4)] ^ RotateLeft(coefficients[Mod5(i + 1)], 1);
				sha3Value[i] ^= one;
				sha3Value[i + 5] ^= one;
				sha3Value[i + 10] ^= one;
				sha3Value[i + 15] ^= one;
				sha3Value[i + 20] ^= one;
			}

			// Rho Pi
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

			// Chi
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

			// Iota
			sha3Value[0] ^= XorMasks[round];
		}
	}

END_NS
