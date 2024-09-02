//
// Created by hsclo on 24. 5. 17.
//

#ifndef ASCII_H
#define ASCII_H

#include <algorithm>
#include <ranges>
#include <cwctype>
#include "Type.h"

BEGIN_NS(Ne)
	class NEBULA_API Ascii final
	{
	private:
		explicit Ascii() = default;
		~Ascii() = default;

	private:
		enum CharProperties
		{
			ACP_CONTROL  = 0x0001,
			ACP_SPACE    = 0x0002,
			ACP_PUNCT    = 0x0004,
			ACP_DIGIT    = 0x0008,
			ACP_HEXDIGIT = 0x0010,
			ACP_ALPHA    = 0x0020,
			ACP_LOWER    = 0x0040,
			ACP_UPPER    = 0x0080,
			ACP_GRAPH    = 0x0100,
			ACP_PRINT    = 0x0200
		};

		static const int_t CharProperties[128];

	public:
		static int_t Properties(int_t _value);
		static bool_t HasSomeProperties(int_t _value, int_t _properties);
		static bool_t HasProperties(int_t _value, int_t _properties);

	public:
		static bool_t IsAscii(int_t _value);
		static bool_t IsSpace(int_t _value);
		static bool_t IsPunct(int_t _value);
		static bool_t IsDigit(int_t _value);
		static bool_t IsHexDigit(int_t _value);
		static bool_t IsAlpha(int_t _value);
		static bool_t IsAlphaNumeric(int_t _value);
		static bool_t IsLower(int_t _value);
		static bool_t IsUpper(int_t _value);

	public:
		static int_t Lower(int_t _value);
		static int_t Upper(int_t _value);
	};

END_NS

typedef Ne::Ascii NebulaAscii;

#endif //ASCII_H
