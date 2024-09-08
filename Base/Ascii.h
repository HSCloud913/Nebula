//
// Created by hsclo on 24. 5. 17.
//

#ifndef NEBULAASCII_H
#define NEBULAASCII_H

#include <algorithm>
#include <ranges>
#include <cwctype>
#include "Type.h"

BEGIN_NS(ne)
    class NEBULA_API Ascii final
    {
    private:
        explicit Ascii() = default;
        ~Ascii() = default;

    private:
        enum CharProperties
        {
            ACP_CONTROL = 0x0001,
            ACP_SPACE = 0x0002,
            ACP_PUNCT = 0x0004,
            ACP_DIGIT = 0x0008,
            ACP_HEXDIGIT = 0x0010,
            ACP_ALPHA = 0x0020,
            ACP_LOWER = 0x0040,
            ACP_UPPER = 0x0080,
            ACP_GRAPH = 0x0100,
            ACP_PRINT = 0x0200
        };

        static const int_t CharProperties[128];

    public:
        [[nodiscard]] inline static int_t Properties(int_t _value);
        [[nodiscard]] inline static bool_t HasProperties(int_t _value, int_t _properties);

    public:
        [[nodiscard]] inline static bool_t IsAscii(int_t _value);
        [[nodiscard]] inline static bool_t IsSpace(int_t _value);
        [[nodiscard]] inline static bool_t IsPunct(int_t _value);
        [[nodiscard]] inline static bool_t IsDigit(int_t _value);
        [[nodiscard]] inline static bool_t IsHexDigit(int_t _value);
        [[nodiscard]] inline static bool_t IsAlpha(int_t _value);
        [[nodiscard]] inline static bool_t IsAlphaNumeric(int_t _value);
        [[nodiscard]] inline static bool_t IsLower(int_t _value);
        [[nodiscard]] inline static bool_t IsUpper(int_t _value);

    public:
        [[nodiscard]] inline static int_t Lower(int_t _value);
        [[nodiscard]] inline static int_t Upper(int_t _value);
    };



    inline int_t Ascii::Properties(int_t _value)
    {
        return (static_cast<uint_t>(_value) < 128 ? CharProperties[_value] : 0);
    }

    inline bool_t Ascii::HasProperties(int_t _value, int_t _properties)
    {
        return (Properties(_value) & _properties) == _properties;
    }


    inline bool_t Ascii::IsAscii(int_t _value)
    {
        return (static_cast<uint_t>(_value) < 128);
    }

    inline bool_t Ascii::IsSpace(int_t _value)
    {
        return HasProperties(_value, ACP_SPACE);
    }

    inline bool_t Ascii::IsPunct(int_t _value)
    {
        return HasProperties(_value, ACP_PUNCT);
    }

    inline bool_t Ascii::IsDigit(int_t _value)
    {
        return HasProperties(_value, ACP_DIGIT);
    }

    inline bool_t Ascii::IsHexDigit(int_t _value)
    {
        return HasProperties(_value, ACP_HEXDIGIT);
    }

    inline bool_t Ascii::IsAlpha(int_t _value)
    {
        return HasProperties(_value, ACP_ALPHA);
    }

    inline bool_t Ascii::IsAlphaNumeric(int_t _value)
    {
        return (HasProperties(_value, ACP_ALPHA) || HasProperties(_value, ACP_DIGIT));
    }

    inline bool_t Ascii::IsLower(int_t _value)
    {
        return HasProperties(_value, ACP_LOWER);
    }

    inline bool_t Ascii::IsUpper(const int_t _value)
    {
        return HasProperties(_value, ACP_UPPER);
    }


    inline int_t Ascii::Lower(const int_t _value)
    {
        return IsUpper(_value) ? _value + 32 : _value;
    }

    inline int_t Ascii::Upper(const int_t _value)
    {
        return IsLower(_value) ? _value - 32 : _value;
    }

END_NS

typedef ne::Ascii NebulaAscii;

#endif //NEBULAASCII_H
