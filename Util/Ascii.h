//
// Created by nebula on 24. 5. 17.
//

#pragma once
#include <algorithm>
#include <ranges>
#include "Base/Type.h"

BEGIN_NS(ne)
	/**
	 * @class Ascii
	 * @brief ASCII 문자의 속성(공백/구두점/숫자/대소문자 등) 판별과 대소문자 변환을 제공하는
	 * 정적 유틸리티 클래스입니다.
	 *
	 * @note 128 미만(ASCII 범위)만 판별 가능하며, 그 외 값은 속성 없음(0)으로 취급합니다.
	 */
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
		[[nodiscard]] static int_t Properties(const int_t _value) { return (static_cast<uint_t>(_value) < 128 ? CharProperties[_value] : 0); }
		[[nodiscard]] static bool_t HasProperties(const int_t _value, const int_t _properties) { return (Properties(_value) & _properties) == _properties; }

	public:
		[[nodiscard]] static bool_t IsAscii(const int_t _value) { return (static_cast<uint_t>(_value) < 128); }
		[[nodiscard]] static bool_t IsSpace(const int_t _value) { return HasProperties(_value, ACP_SPACE); }
		[[nodiscard]] static bool_t IsPunct(const int_t _value) { return HasProperties(_value, ACP_PUNCT); }
		[[nodiscard]] static bool_t IsDigit(const int_t _value) { return HasProperties(_value, ACP_DIGIT); }
		[[nodiscard]] static bool_t IsHexDigit(const int_t _value) { return HasProperties(_value, ACP_HEXDIGIT); }
		[[nodiscard]] static bool_t IsAlpha(const int_t _value) { return HasProperties(_value, ACP_ALPHA); }
		[[nodiscard]] static bool_t IsAlphaNumeric(const int_t _value) { return (Properties(_value) & (ACP_ALPHA | ACP_DIGIT)) != 0; }
		[[nodiscard]] static bool_t IsLower(const int_t _value) { return HasProperties(_value, ACP_LOWER); }
		[[nodiscard]] static bool_t IsUpper(const int_t _value) { return HasProperties(_value, ACP_UPPER); }
		[[nodiscard]] static bool_t IsControl(const int_t _value) { return HasProperties(_value, ACP_CONTROL); }
		[[nodiscard]] static bool_t IsGraph(const int_t _value) { return HasProperties(_value, ACP_GRAPH); }
		[[nodiscard]] static bool_t IsPrint(const int_t _value) { return HasProperties(_value, ACP_PRINT); }

	public:
		[[nodiscard]] static int_t Lower(const int_t _value) { return IsUpper(_value) ? _value + 32 : _value; }
		[[nodiscard]] static int_t Upper(const int_t _value) { return IsLower(_value) ? _value - 32 : _value; }
	};

END_NS
