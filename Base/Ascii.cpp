//
// Created by hsclo on 24. 5. 17.
//

#include "Ascii.h"


BEGIN_NS(Ne)
    const int_t Ascii::CharProperties[128] =
        {
            /* 00 . */ ACP_CONTROL,
            /* 01 . */ ACP_CONTROL,
            /* 02 . */ ACP_CONTROL,
            /* 03 . */ ACP_CONTROL,
            /* 04 . */ ACP_CONTROL,
            /* 05 . */ ACP_CONTROL,
            /* 06 . */ ACP_CONTROL,
            /* 07 . */ ACP_CONTROL,
            /* 08 . */ ACP_CONTROL,
            /* 09 . */ ACP_CONTROL | ACP_SPACE,
            /* 0a . */ ACP_CONTROL | ACP_SPACE,
            /* 0b . */ ACP_CONTROL | ACP_SPACE,
            /* 0c . */ ACP_CONTROL | ACP_SPACE,
            /* 0d . */ ACP_CONTROL | ACP_SPACE,
            /* 0e . */ ACP_CONTROL,
            /* 0f . */ ACP_CONTROL,
            /* 10 . */ ACP_CONTROL,
            /* 11 . */ ACP_CONTROL,
            /* 12 . */ ACP_CONTROL,
            /* 13 . */ ACP_CONTROL,
            /* 14 . */ ACP_CONTROL,
            /* 15 . */ ACP_CONTROL,
            /* 16 . */ ACP_CONTROL,
            /* 17 . */ ACP_CONTROL,
            /* 18 . */ ACP_CONTROL,
            /* 19 . */ ACP_CONTROL,
            /* 1a . */ ACP_CONTROL,
            /* 1b . */ ACP_CONTROL,
            /* 1c . */ ACP_CONTROL,
            /* 1d . */ ACP_CONTROL,
            /* 1e . */ ACP_CONTROL,
            /* 1f . */ ACP_CONTROL,
            /* 20   */ ACP_SPACE | ACP_PRINT,
            /* 21 ! */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 22 " */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 23 # */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 24 $ */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 25 % */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 26 & */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 27 ' */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 28 ( */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 29 ) */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 2a * */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 2b + */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 2c , */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 2d - */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 2e . */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 2f / */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 30 0 */ ACP_DIGIT | ACP_HEXDIGIT | ACP_GRAPH | ACP_PRINT,
            /* 31 1 */ ACP_DIGIT | ACP_HEXDIGIT | ACP_GRAPH | ACP_PRINT,
            /* 32 2 */ ACP_DIGIT | ACP_HEXDIGIT | ACP_GRAPH | ACP_PRINT,
            /* 33 3 */ ACP_DIGIT | ACP_HEXDIGIT | ACP_GRAPH | ACP_PRINT,
            /* 34 4 */ ACP_DIGIT | ACP_HEXDIGIT | ACP_GRAPH | ACP_PRINT,
            /* 35 5 */ ACP_DIGIT | ACP_HEXDIGIT | ACP_GRAPH | ACP_PRINT,
            /* 36 6 */ ACP_DIGIT | ACP_HEXDIGIT | ACP_GRAPH | ACP_PRINT,
            /* 37 7 */ ACP_DIGIT | ACP_HEXDIGIT | ACP_GRAPH | ACP_PRINT,
            /* 38 8 */ ACP_DIGIT | ACP_HEXDIGIT | ACP_GRAPH | ACP_PRINT,
            /* 39 9 */ ACP_DIGIT | ACP_HEXDIGIT | ACP_GRAPH | ACP_PRINT,
            /* 3a : */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 3b ; */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 3c < */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 3d = */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 3e > */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 3f ? */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 40 @ */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 41 A */ ACP_HEXDIGIT | ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 42 B */ ACP_HEXDIGIT | ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 43 C */ ACP_HEXDIGIT | ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 44 D */ ACP_HEXDIGIT | ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 45 E */ ACP_HEXDIGIT | ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 46 F */ ACP_HEXDIGIT | ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 47 G */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 48 H */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 49 I */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 4a J */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 4b K */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 4c L */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 4d M */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 4e N */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 4f O */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 50 P */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 51 Q */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 52 R */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 53 S */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 54 T */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 55 U */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 56 V */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 57 W */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 58 X */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 59 Y */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 5a Z */ ACP_ALPHA | ACP_UPPER | ACP_GRAPH | ACP_PRINT,
            /* 5b [ */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 5c \ */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 5d ] */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 5e ^ */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 5f _ */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 60 ` */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 61 a */ ACP_HEXDIGIT | ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 62 b */ ACP_HEXDIGIT | ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 63 c */ ACP_HEXDIGIT | ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 64 d */ ACP_HEXDIGIT | ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 65 e */ ACP_HEXDIGIT | ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 66 f */ ACP_HEXDIGIT | ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 67 g */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 68 h */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 69 i */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 6a j */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 6b k */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 6c l */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 6d m */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 6e n */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 6f o */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 70 p */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 71 q */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 72 r */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 73 s */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 74 t */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 75 u */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 76 v */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 77 w */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 78 x */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 79 y */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 7a z */ ACP_ALPHA | ACP_LOWER | ACP_GRAPH | ACP_PRINT,
            /* 7b { */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 7c | */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 7d } */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 7e ~ */ ACP_PUNCT | ACP_GRAPH | ACP_PRINT,
            /* 7f . */ ACP_CONTROL
        };


    int_t Ascii::Properties(int_t _value)
    {
        return IsAscii(_value) ? CharProperties[_value] : 0;
    }

    bool_t Ascii::HasSomeProperties(int_t _value, int_t _properties)
    {
        return (Properties(_value) & _properties) != 0;
    }

    bool_t Ascii::HasProperties(int_t _value, int_t _properties)
    {
        return (Properties(_value) & _properties) == _properties;
    }


    bool_t Ascii::IsAscii(int_t _value)
    {
        return (static_cast<uint_t>(_value) & 0xFFFFFF80) == 0;
    }

    bool_t Ascii::IsSpace(int_t _value)
    {
        return HasProperties(_value, ACP_SPACE);
    }

    bool_t Ascii::IsPunct(int_t _value)
    {
        return HasProperties(_value, ACP_PUNCT);
    }

    bool_t Ascii::IsDigit(int_t _value)
    {
        return HasProperties(_value, ACP_DIGIT);
    }

    bool_t Ascii::IsHexDigit(int_t _value)
    {
        return HasProperties(_value, ACP_HEXDIGIT);
    }

    bool_t Ascii::IsAlpha(int_t _value)
    {
        return HasProperties(_value, ACP_ALPHA);
    }

    bool_t Ascii::IsAlphaNumeric(int_t _value)
    {
        return HasSomeProperties(_value, ACP_ALPHA | ACP_DIGIT);
    }

    bool_t Ascii::IsLower(int_t _value)
    {
        return HasProperties(_value, ACP_LOWER);
    }

    bool_t Ascii::IsUpper(const int_t _value)
    {
        return HasProperties(_value, ACP_UPPER);
    }


    int_t Ascii::Lower(const int_t _value)
    {
        return IsUpper(_value) ? _value + 32 : _value;
    }

    int_t Ascii::Upper(const int_t _value)
    {
        return IsLower(_value) ? _value - 32 : _value;
    }
END_NS
