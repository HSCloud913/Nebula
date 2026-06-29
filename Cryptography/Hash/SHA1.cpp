#include "SHA1.h"

#include <cstring>



inline ne::uint_t SHA1_F1(ne::uint_t b, ne::uint_t c, ne::uint_t d)
{
	return d ^ (b & (c ^ d)); // original: f = (b & c) | ((~b) & d);
}
inline ne::uint_t SHA1_F2(ne::uint_t b, ne::uint_t c, ne::uint_t d)
{
	return b ^ c ^ d;
}
inline ne::uint_t SHA1_F3(ne::uint_t b, ne::uint_t c, ne::uint_t d)
{
	return (b & c) | (b & d) | (c & d);
}
inline ne::uint_t SHA1_Rotate(ne::uint_t a, ne::uint_t c)
{
	return (a << c) | (a >> (32 - c));
}



BEGIN_NS(ne::cryptography)
	void SHA1::Init()
	{
		memset(buffer, 0, sizeof(byte_t) * Sha1BlockSize);
		bufferSize = 0;
		length = 0;

		sha1Value[0] = 0x67452301;
		sha1Value[1] = 0xefcdab89;
		sha1Value[2] = 0x98badcfe;
		sha1Value[3] = 0x10325476;
		sha1Value[4] = 0xc3d2e1f0;
	}

	void SHA1::AddBuffer(const void_t* _data, size_t _dataLength)
	{
		const auto* data = static_cast<const byte_t*>(_data);

		if (bufferSize > 0)
		{
			while (_dataLength > 0 && bufferSize < Sha1BlockSize)
			{
				buffer[bufferSize++] = *data++;
				_dataLength--;
			}
		}

		if (bufferSize == Sha1BlockSize)
		{
			ProcessBlock(buffer);
			length += Sha1BlockSize;
			bufferSize = 0;
		}

		if (_dataLength == 0) return;

		while (_dataLength >= Sha1BlockSize)
		{
			ProcessBlock(data);
			data += Sha1BlockSize;
			length += Sha1BlockSize;
			_dataLength -= Sha1BlockSize;
		}

		while (_dataLength > 0)
		{
			buffer[bufferSize++] = *data++;
			_dataLength--;
		}
	}

	string_t SHA1::Get()
	{
		ProcessBuffer();

		byte_t tempValue[Sha1HashBytes];
		for (auto i = 0; i < Sha1HashValues; i++)
		{
			tempValue[(i + 1) * 4 - 4] = (sha1Value[i] >> 24) & 0xFF;
			tempValue[(i + 1) * 4 - 3] = (sha1Value[i] >> 16) & 0xFF;
			tempValue[(i + 1) * 4 - 2] = (sha1Value[i] >> 8) & 0xFF;
			tempValue[(i + 1) * 4 - 1] = sha1Value[i] & 0xFF;
		}

		constexpr char_t format[] = "0123456789abcdef";

		string_t result;
		for (auto i = 0; i < Sha1HashBytes; i++)
		{
			result += format[(tempValue[i] >> 4) & 15];
			result += format[tempValue[i] & 15];
		}

		return result;
	}



	void SHA1::ProcessBuffer()
	{
		size_t paddedLength = bufferSize * 8;

		paddedLength++;

		size_t lower11Bits = paddedLength & 511;
		if (lower11Bits <= 448)
		{
			paddedLength += 448 - lower11Bits;
		}
		else
		{
			paddedLength += 512 + 448 - lower11Bits;
		}
		paddedLength /= 8;

		byte_t extra[Sha1BlockSize];
		if (bufferSize < Sha1BlockSize)
		{
			buffer[bufferSize] = 128;
		}
		else
		{
			extra[0] = 128;
		}

		size_t i;
		for (i = bufferSize + 1; i < Sha1BlockSize; i++)
		{
			buffer[i] = 0;
		}
		for (; i < paddedLength; i++)
		{
			extra[i - Sha1BlockSize] = 0;
		}

		byte_t* addLength;
		if (paddedLength < Sha1BlockSize)
		{
			addLength = buffer + paddedLength;
		}
		else
		{
			addLength = extra + paddedLength - Sha1BlockSize;
		}

		ulonglong_t msgBits = 8 * (length + bufferSize);
		for (int_t a = 0; a < 7; a++)
		{
			*addLength++ = static_cast<byte_t>((msgBits >> (56 - (a * 8))) & 0xFF);
		}

		*addLength = static_cast<byte_t>(msgBits & 0xFF);

		ProcessBlock(buffer);
		if (paddedLength > Sha1BlockSize)
		{
			ProcessBlock(extra);
		}
	}

	void SHA1::ProcessBlock(const void_t* _data)
	{
		auto data = static_cast<const uint_t*>(_data);
		uint_t words[80] = { 0, };

		for (int_t i = 0; i < 16; i++)
		{
			words[i] = (data[i] >> 24) | ((data[i] >> 8) & 0x0000FF00) | ((data[i] << 8) & 0x00FF0000) | (data[i] << 24);
		}

		for (int_t i = 16; i < 80; i++)
		{
			words[i] = SHA1_Rotate(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
		}

		uint_t a = sha1Value[0];
		uint_t b = sha1Value[1];
		uint_t c = sha1Value[2];
		uint_t d = sha1Value[3];
		uint_t e = sha1Value[4];

		int_t offset = 0;

		// first round
		for (int_t i = 0; i < 4; i++)
		{
			offset = 5 * i;
			e += SHA1_Rotate(a, 5) + SHA1_F1(b, c, d) + words[offset] + 0x5a827999;
			b = SHA1_Rotate(b, 30);
			d += SHA1_Rotate(e, 5) + SHA1_F1(a, b, c) + words[offset + 1] + 0x5a827999;
			a = SHA1_Rotate(a, 30);
			c += SHA1_Rotate(d, 5) + SHA1_F1(e, a, b) + words[offset + 2] + 0x5a827999;
			e = SHA1_Rotate(e, 30);
			b += SHA1_Rotate(c, 5) + SHA1_F1(d, e, a) + words[offset + 3] + 0x5a827999;
			d = SHA1_Rotate(d, 30);
			a += SHA1_Rotate(b, 5) + SHA1_F1(c, d, e) + words[offset + 4] + 0x5a827999;
			c = SHA1_Rotate(c, 30);
		}

		// second round
		for (int_t i = 4; i < 8; i++)
		{
			offset = 5 * i;
			e += SHA1_Rotate(a, 5) + SHA1_F2(b, c, d) + words[offset] + 0x6ed9eba1;
			b = SHA1_Rotate(b, 30);
			d += SHA1_Rotate(e, 5) + SHA1_F2(a, b, c) + words[offset + 1] + 0x6ed9eba1;
			a = SHA1_Rotate(a, 30);
			c += SHA1_Rotate(d, 5) + SHA1_F2(e, a, b) + words[offset + 2] + 0x6ed9eba1;
			e = SHA1_Rotate(e, 30);
			b += SHA1_Rotate(c, 5) + SHA1_F2(d, e, a) + words[offset + 3] + 0x6ed9eba1;
			d = SHA1_Rotate(d, 30);
			a += SHA1_Rotate(b, 5) + SHA1_F2(c, d, e) + words[offset + 4] + 0x6ed9eba1;
			c = SHA1_Rotate(c, 30);
		}

		// third round
		for (int_t i = 8; i < 12; i++)
		{
			offset = 5 * i;
			e += SHA1_Rotate(a, 5) + SHA1_F3(b, c, d) + words[offset] + 0x8f1bbcdc;
			b = SHA1_Rotate(b, 30);
			d += SHA1_Rotate(e, 5) + SHA1_F3(a, b, c) + words[offset + 1] + 0x8f1bbcdc;
			a = SHA1_Rotate(a, 30);
			c += SHA1_Rotate(d, 5) + SHA1_F3(e, a, b) + words[offset + 2] + 0x8f1bbcdc;
			e = SHA1_Rotate(e, 30);
			b += SHA1_Rotate(c, 5) + SHA1_F3(d, e, a) + words[offset + 3] + 0x8f1bbcdc;
			d = SHA1_Rotate(d, 30);
			a += SHA1_Rotate(b, 5) + SHA1_F3(c, d, e) + words[offset + 4] + 0x8f1bbcdc;
			c = SHA1_Rotate(c, 30);
		}

		// fourth round
		for (int_t i = 12; i < 16; i++)
		{
			offset = 5 * i;
			e += SHA1_Rotate(a, 5) + SHA1_F2(b, c, d) + words[offset] + 0xca62c1d6;
			b = SHA1_Rotate(b, 30);
			d += SHA1_Rotate(e, 5) + SHA1_F2(a, b, c) + words[offset + 1] + 0xca62c1d6;
			a = SHA1_Rotate(a, 30);
			c += SHA1_Rotate(d, 5) + SHA1_F2(e, a, b) + words[offset + 2] + 0xca62c1d6;
			e = SHA1_Rotate(e, 30);
			b += SHA1_Rotate(c, 5) + SHA1_F2(d, e, a) + words[offset + 3] + 0xca62c1d6;
			d = SHA1_Rotate(d, 30);
			a += SHA1_Rotate(b, 5) + SHA1_F2(c, d, e) + words[offset + 4] + 0xca62c1d6;
			c = SHA1_Rotate(c, 30);
		}

		// update hash
		sha1Value[0] += a;
		sha1Value[1] += b;
		sha1Value[2] += c;
		sha1Value[3] += d;
		sha1Value[4] += e;
	}

END_NS
