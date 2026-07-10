#include "Util/Base64.h"

#include <cstring>



constexpr ne::string_view_t Base64String = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";



BEGIN_NS(ne)
	void_t Base64::Encode(lpcstr_t _string, char_t* _buffer, size_t _bufferSize)
	{
		byte_t arrayInput[3], arrayOutput[4];

		int_t i = 0;
		size_t pos = 0;

		size_t len = strlen(_string);
		while (len--)
		{
			arrayInput[i++] = static_cast<byte_t>(*_string++);

			if (i == 3)
			{
				arrayOutput[0] = (arrayInput[0] & 0xfc) >> 2;
				arrayOutput[1] = ((arrayInput[0] & 0x03) << 4) + ((arrayInput[1] & 0xf0) >> 4);
				arrayOutput[2] = ((arrayInput[1] & 0x0f) << 2) + ((arrayInput[2] & 0xc0) >> 6);
				arrayOutput[3] = arrayInput[2] & 0x3f;

				for (i = 0; (i < 4); i++) { if (pos < _bufferSize) { _buffer[pos++] = Base64String[arrayOutput[i]]; } }

				i = 0;
			}
		}

		if (i)
		{
			for (int_t j = i; j < 3; j++) { arrayInput[j] = '\0'; }

			arrayOutput[0] = (arrayInput[0] & 0xfc) >> 2;
			arrayOutput[1] = ((arrayInput[0] & 0x03) << 4) + ((arrayInput[1] & 0xf0) >> 4);
			arrayOutput[2] = ((arrayInput[1] & 0x0f) << 2) + ((arrayInput[2] & 0xc0) >> 6);
			arrayOutput[3] = arrayInput[2] & 0x3f;

			for (int_t j = 0; j < (i + 1); j++) { if (pos < _bufferSize) { _buffer[pos++] = Base64String[arrayOutput[j]]; } }

			while ((i++ < 3)) { if (pos < _bufferSize) { _buffer[pos++] = '='; } }
		}
	}

	void_t Base64::Decode(lpcstr_t _string, char_t* _buffer, size_t _bufferSize)
	{
		byte_t arrayInput[4], arrayOutput[3];

		size_t i = 0;
		size_t pos = 0;
		int_t idx = 0;

		size_t cchLen = strlen(_string);
		while (cchLen-- && (_string[idx] != '='))
		{
			arrayInput[i++] = static_cast<byte_t>(_string[idx++]);

			if (i == 4)
			{
				for (i = 0; i < 4; i++) { arrayInput[i] = static_cast<byte_t>(Base64String.find(arrayInput[i])); }

				arrayOutput[0] = (arrayInput[0] << 2) + ((arrayInput[1] & 0x30) >> 4);
				arrayOutput[1] = ((arrayInput[1] & 0xf) << 4) + ((arrayInput[2] & 0x3c) >> 2);
				arrayOutput[2] = ((arrayInput[2] & 0x3) << 6) + arrayInput[3];

				for (i = 0; (i < 3); i++) { if (pos < _bufferSize) { _buffer[pos++] = arrayOutput[i]; } }

				i = 0;
			}
		}

		if (i)
		{
			for (size_t j = i; j < 4; j++) { arrayInput[j] = 0; }

			for (size_t j = 0; j < 4; j++) { arrayInput[j] = static_cast<byte_t>(Base64String.find(arrayInput[j])); }

			arrayOutput[0] = (arrayInput[0] << 2) + ((arrayInput[1] & 0x30) >> 4);
			arrayOutput[1] = ((arrayInput[1] & 0xf) << 4) + ((arrayInput[2] & 0x3c) >> 2);
			arrayOutput[2] = ((arrayInput[2] & 0x3) << 6) + arrayInput[3];

			for (size_t j = 0; j < (i - 1); j++) { if (pos < _bufferSize) { _buffer[pos++] = arrayOutput[j]; } }
		}
	}


	string_t Base64::Encode(string_t&& _string)
	{
		string_t encodeString;

		byte_t arrayInput[3], arrayOutput[4];

		int_t i = 0;
		size_t pos = 0;

		size_t len = _string.size();

		while (len--)
		{
			arrayInput[i++] = static_cast<byte_t>(_string[pos++]);

			if (i == 3)
			{
				arrayOutput[0] = (arrayInput[0] & 0xfc) >> 2;
				arrayOutput[1] = ((arrayInput[0] & 0x03) << 4) + ((arrayInput[1] & 0xf0) >> 4);
				arrayOutput[2] = ((arrayInput[1] & 0x0f) << 2) + ((arrayInput[2] & 0xc0) >> 6);
				arrayOutput[3] = arrayInput[2] & 0x3f;

				for (i = 0; (i < 4); i++) { encodeString += Base64String[arrayOutput[i]]; }

				i = 0;
			}
		}

		if (i)
		{
			for (int_t j = i; j < 3; j++) { arrayInput[j] = '\0'; }

			arrayOutput[0] = (arrayInput[0] & 0xfc) >> 2;
			arrayOutput[1] = ((arrayInput[0] & 0x03) << 4) + ((arrayInput[1] & 0xf0) >> 4);
			arrayOutput[2] = ((arrayInput[1] & 0x0f) << 2) + ((arrayInput[2] & 0xc0) >> 6);
			arrayOutput[3] = arrayInput[2] & 0x3f;

			for (int_t j = 0; j < (i + 1); j++) { encodeString += Base64String[arrayOutput[j]]; }

			while ((i++ < 3)) { encodeString += '='; }
		}

		return encodeString;
	}

	string_t Base64::Decode(string_t&& _string)
	{
		string_t generalString;

		byte_t byArrayInput[4], byArrayOutput[3];

		size_t cchLen = _string.size();
		size_t i = 0;
		int_t idx = 0;

		while (cchLen-- && (_string[idx] != '='))
		{
			byArrayInput[i++] = static_cast<byte_t>(_string[idx++]);

			if (i == 4)
			{
				for (i = 0; i < 4; i++) { byArrayInput[i] = static_cast<byte_t>(Base64String.find(byArrayInput[i])); }

				byArrayOutput[0] = (byArrayInput[0] << 2) + ((byArrayInput[1] & 0x30) >> 4);
				byArrayOutput[1] = ((byArrayInput[1] & 0xf) << 4) + ((byArrayInput[2] & 0x3c) >> 2);
				byArrayOutput[2] = ((byArrayInput[2] & 0x3) << 6) + byArrayInput[3];

				for (i = 0; (i < 3); i++) { generalString += byArrayOutput[i]; }

				i = 0;
			}
		}

		if (i)
		{
			for (size_t j = i; j < 4; j++) { byArrayInput[j] = 0; }

			for (size_t j = 0; j < 4; j++) { byArrayInput[j] = static_cast<byte_t>(Base64String.find(byArrayInput[j])); }

			byArrayOutput[0] = (byArrayInput[0] << 2) + ((byArrayInput[1] & 0x30) >> 4);
			byArrayOutput[1] = ((byArrayInput[1] & 0xf) << 4) + ((byArrayInput[2] & 0x3c) >> 2);
			byArrayOutput[2] = ((byArrayInput[2] & 0x3) << 6) + byArrayInput[3];

			for (size_t j = 0; j < (i - 1); j++) { generalString += byArrayOutput[j]; }
		}

		return generalString;
	}


	string_t Base64::EncodeURL(string_t&& _string)
	{
		string_t result = Encode(std::move(_string));

		for (char_t& c : result)
		{
			if (c == '+') c = '-';
			else if (c == '/') c = '_';
		}

		const size_t padPos = result.find('=');
		if (padPos != string_t::npos) result.erase(padPos);

		return result;
	}

	string_t Base64::DecodeURL(string_t&& _string)
	{
		string_t padded = std::move(_string);

		for (char_t& c : padded)
		{
			if (c == '-') c = '+';
			else if (c == '_') c = '/';
		}

		const size_t remainder = padded.size() % 4;
		if (remainder == 2) padded += "==";
		else if (remainder == 3) padded += "=";

		return Decode(std::move(padded));
	}

END_NS
