#include "MD5.h"

#include <cstring>



inline ne::uint_t MD5_F1(ne::uint_t b, ne::uint_t c, ne::uint_t d)
{
	return d ^ (b & (c ^ d)); // original: f = (b & c) | ((~b) & d);
}
inline ne::uint_t MD5_F2(ne::uint_t b, ne::uint_t c, ne::uint_t d)
{
	return c ^ (d & (b ^ c)); // original: f = (b & d) | (c & (~d));
}
inline ne::uint_t MD5_F3(ne::uint_t b, ne::uint_t c, ne::uint_t d)
{
	return b ^ c ^ d;
}
inline ne::uint_t MD5_F4(ne::uint_t b, ne::uint_t c, ne::uint_t d)
{
	return c ^ (b | ~d);
}
inline ne::uint_t MD5_Rotate(ne::uint_t a, ne::uint_t c)
{
	return (a << c) | (a >> (32 - c));
}



BEGIN_NS(ne::cryptography)
	void MD5::Init()
	{
		md5Value[0] = 0x67452301;
		md5Value[1] = 0xefcdab89;
		md5Value[2] = 0x98badcfe;
		md5Value[3] = 0x10325476;
	}

	void MD5::AddBuffer(const void* _data, size_t _dataLen)
	{
		const auto* data = static_cast<const byte_t*>(_data);

		if (bufferSize > 0)
		{
			while (_dataLen > 0 && bufferSize < MD5BlockSize)
			{
				buffer[bufferSize++] = *data++;
				_dataLen--;
			}
		}

		if (bufferSize == MD5BlockSize)
		{
			ProcessBlock(buffer);
			length += MD5BlockSize;
			bufferSize = 0;
		}

		if (_dataLen == 0)
		{
			return;
		}

		while (_dataLen >= MD5BlockSize)
		{
			ProcessBlock(data);
			data += MD5BlockSize;
			length += MD5BlockSize;
			_dataLen -= MD5BlockSize;
		}

		while (_dataLen > 0)
		{
			buffer[bufferSize++] = *data++;
			_dataLen--;
		}
	}


	string_t MD5::Get()
	{
		ProcessBuffer();

		byte_t md5TmpVal[MD5HashBytes];
		for (int_t i = 0; i < MD5HashValues; i++)
		{
			md5TmpVal[(i + 1) * 4 - 4] = md5Value[i] & 0xFF;
			md5TmpVal[(i + 1) * 4 - 3] = (md5Value[i] >> 8) & 0xFF;
			md5TmpVal[(i + 1) * 4 - 2] = (md5Value[i] >> 16) & 0xFF;
			md5TmpVal[(i + 1) * 4 - 1] = (md5Value[i] >> 24) & 0xFF;
		}

		constexpr char_t format[] = "0123456789abcdef";

		string_t result;
		for (int_t i = 0; i < MD5HashBytes; i++)
		{
			result += format[(md5TmpVal[i] >> 4) & 15];
			result += format[md5TmpVal[i] & 15];
		}

		return result;
	}



	void MD5::ProcessBuffer()
	{
		size_t paddedLen = bufferSize * 8;

		paddedLen++;

		size_t lower11Bits = paddedLen & 511;
		if (lower11Bits <= 448)
		{
			paddedLen += 448 - lower11Bits;
		}
		else
		{
			paddedLen += 512 + 448 - lower11Bits;
		}
		paddedLen /= 8;

		byte_t extra[MD5BlockSize];
		if (bufferSize < MD5BlockSize)
		{
			buffer[bufferSize] = 128;
		}
		else
		{
			extra[0] = 128;
		}

		size_t i;
		for (i = bufferSize + 1; i < MD5BlockSize; i++)
		{
			buffer[i] = 0;
		}
		for (; i < paddedLen; i++)
		{
			extra[i - MD5BlockSize] = 0;
		}

		byte_t* addLength;
		if (paddedLen < MD5BlockSize)
		{
			addLength = buffer + paddedLen;
		}
		else
		{
			addLength = extra + paddedLen - MD5BlockSize;
		}

		ulonglong_t msgBits = 8 * (length + bufferSize);
		for (int_t a = 0; a < 7; a++)
		{
			*addLength++ = static_cast<byte_t>(msgBits & 0xFF);
			msgBits >>= 8;
		}

		*addLength++ = static_cast<byte_t>(msgBits & 0xFF);

		ProcessBlock(buffer);
		if (paddedLen > MD5BlockSize)
		{
			ProcessBlock(extra);
		}
	}


	void MD5::ProcessBlock(const void* _data)
	{
		auto data = static_cast<const uint_t*>(_data);
		uint_t words[16] = { 0, };

		for (int_t i = 0; i < 16; i++)
		{
			words[i] = data[i];
		}

		uint_t a = md5Value[0];
		uint_t b = md5Value[1];
		uint_t c = md5Value[2];
		uint_t d = md5Value[3];

		// first round
		a = MD5_Rotate(a + MD5_F1(b, c, d) + words[0] + 0xd76aa478, 7) + b;
		d = MD5_Rotate(d + MD5_F1(a, b, c) + words[1] + 0xe8c7b756, 12) + a;
		c = MD5_Rotate(c + MD5_F1(d, a, b) + words[2] + 0x242070db, 17) + d;
		b = MD5_Rotate(b + MD5_F1(c, d, a) + words[3] + 0xc1bdceee, 22) + c;

		a = MD5_Rotate(a + MD5_F1(b, c, d) + words[4] + 0xf57c0faf, 7) + b;
		d = MD5_Rotate(d + MD5_F1(a, b, c) + words[5] + 0x4787c62a, 12) + a;
		c = MD5_Rotate(c + MD5_F1(d, a, b) + words[6] + 0xa8304613, 17) + d;
		b = MD5_Rotate(b + MD5_F1(c, d, a) + words[7] + 0xfd469501, 22) + c;

		a = MD5_Rotate(a + MD5_F1(b, c, d) + words[8] + 0x698098d8, 7) + b;
		d = MD5_Rotate(d + MD5_F1(a, b, c) + words[9] + 0x8b44f7af, 12) + a;
		c = MD5_Rotate(c + MD5_F1(d, a, b) + words[10] + 0xffff5bb1, 17) + d;
		b = MD5_Rotate(b + MD5_F1(c, d, a) + words[11] + 0x895cd7be, 22) + c;

		a = MD5_Rotate(a + MD5_F1(b, c, d) + words[12] + 0x6b901122, 7) + b;
		d = MD5_Rotate(d + MD5_F1(a, b, c) + words[13] + 0xfd987193, 12) + a;
		c = MD5_Rotate(c + MD5_F1(d, a, b) + words[14] + 0xa679438e, 17) + d;
		b = MD5_Rotate(b + MD5_F1(c, d, a) + words[15] + 0x49b40821, 22) + c;


		// second round
		a = MD5_Rotate(a + MD5_F2(b, c, d) + words[1] + 0xf61e2562, 5) + b;
		d = MD5_Rotate(d + MD5_F2(a, b, c) + words[6] + 0xc040b340, 9) + a;
		c = MD5_Rotate(c + MD5_F2(d, a, b) + words[11] + 0x265e5a51, 14) + d;
		b = MD5_Rotate(b + MD5_F2(c, d, a) + words[0] + 0xe9b6c7aa, 20) + c;

		a = MD5_Rotate(a + MD5_F2(b, c, d) + words[5] + 0xd62f105d, 5) + b;
		d = MD5_Rotate(d + MD5_F2(a, b, c) + words[10] + 0x02441453, 9) + a;
		c = MD5_Rotate(c + MD5_F2(d, a, b) + words[15] + 0xd8a1e681, 14) + d;
		b = MD5_Rotate(b + MD5_F2(c, d, a) + words[4] + 0xe7d3fbc8, 20) + c;

		a = MD5_Rotate(a + MD5_F2(b, c, d) + words[9] + 0x21e1cde6, 5) + b;
		d = MD5_Rotate(d + MD5_F2(a, b, c) + words[14] + 0xc33707d6, 9) + a;
		c = MD5_Rotate(c + MD5_F2(d, a, b) + words[3] + 0xf4d50d87, 14) + d;
		b = MD5_Rotate(b + MD5_F2(c, d, a) + words[8] + 0x455a14ed, 20) + c;

		a = MD5_Rotate(a + MD5_F2(b, c, d) + words[13] + 0xa9e3e905, 5) + b;
		d = MD5_Rotate(d + MD5_F2(a, b, c) + words[2] + 0xfcefa3f8, 9) + a;
		c = MD5_Rotate(c + MD5_F2(d, a, b) + words[7] + 0x676f02d9, 14) + d;
		b = MD5_Rotate(b + MD5_F2(c, d, a) + words[12] + 0x8d2a4c8a, 20) + c;

		// third round
		a = MD5_Rotate(a + MD5_F3(b, c, d) + words[5] + 0xfffa3942, 4) + b;
		d = MD5_Rotate(d + MD5_F3(a, b, c) + words[8] + 0x8771f681, 11) + a;
		c = MD5_Rotate(c + MD5_F3(d, a, b) + words[11] + 0x6d9d6122, 16) + d;
		b = MD5_Rotate(b + MD5_F3(c, d, a) + words[14] + 0xfde5380c, 23) + c;

		a = MD5_Rotate(a + MD5_F3(b, c, d) + words[1] + 0xa4beea44, 4) + b;
		d = MD5_Rotate(d + MD5_F3(a, b, c) + words[4] + 0x4bdecfa9, 11) + a;
		c = MD5_Rotate(c + MD5_F3(d, a, b) + words[7] + 0xf6bb4b60, 16) + d;
		b = MD5_Rotate(b + MD5_F3(c, d, a) + words[10] + 0xbebfbc70, 23) + c;

		a = MD5_Rotate(a + MD5_F3(b, c, d) + words[13] + 0x289b7ec6, 4) + b;
		d = MD5_Rotate(d + MD5_F3(a, b, c) + words[0] + 0xeaa127fa, 11) + a;
		c = MD5_Rotate(c + MD5_F3(d, a, b) + words[3] + 0xd4ef3085, 16) + d;
		b = MD5_Rotate(b + MD5_F3(c, d, a) + words[6] + 0x04881d05, 23) + c;

		a = MD5_Rotate(a + MD5_F3(b, c, d) + words[9] + 0xd9d4d039, 4) + b;
		d = MD5_Rotate(d + MD5_F3(a, b, c) + words[12] + 0xe6db99e5, 11) + a;
		c = MD5_Rotate(c + MD5_F3(d, a, b) + words[15] + 0x1fa27cf8, 16) + d;
		b = MD5_Rotate(b + MD5_F3(c, d, a) + words[2] + 0xc4ac5665, 23) + c;

		// fourth round
		a = MD5_Rotate(a + MD5_F4(b, c, d) + words[0] + 0xf4292244, 6) + b;
		d = MD5_Rotate(d + MD5_F4(a, b, c) + words[7] + 0x432aff97, 10) + a;
		c = MD5_Rotate(c + MD5_F4(d, a, b) + words[14] + 0xab9423a7, 15) + d;
		b = MD5_Rotate(b + MD5_F4(c, d, a) + words[5] + 0xfc93a039, 21) + c;

		a = MD5_Rotate(a + MD5_F4(b, c, d) + words[12] + 0x655b59c3, 6) + b;
		d = MD5_Rotate(d + MD5_F4(a, b, c) + words[3] + 0x8f0ccc92, 10) + a;
		c = MD5_Rotate(c + MD5_F4(d, a, b) + words[10] + 0xffeff47d, 15) + d;
		b = MD5_Rotate(b + MD5_F4(c, d, a) + words[1] + 0x85845dd1, 21) + c;

		a = MD5_Rotate(a + MD5_F4(b, c, d) + words[8] + 0x6fa87e4f, 6) + b;
		d = MD5_Rotate(d + MD5_F4(a, b, c) + words[15] + 0xfe2ce6e0, 10) + a;
		c = MD5_Rotate(c + MD5_F4(d, a, b) + words[6] + 0xa3014314, 15) + d;
		b = MD5_Rotate(b + MD5_F4(c, d, a) + words[13] + 0x4e0811a1, 21) + c;

		a = MD5_Rotate(a + MD5_F4(b, c, d) + words[4] + 0xf7537e82, 6) + b;
		d = MD5_Rotate(d + MD5_F4(a, b, c) + words[11] + 0xbd3af235, 10) + a;
		c = MD5_Rotate(c + MD5_F4(d, a, b) + words[2] + 0x2ad7d2bb, 15) + d;
		b = MD5_Rotate(b + MD5_F4(c, d, a) + words[9] + 0xeb86d391, 21) + c;

		// update hash
		md5Value[0] += a;
		md5Value[1] += b;
		md5Value[2] += c;
		md5Value[3] += d;
	}

END_NS
