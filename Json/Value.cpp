#include "Json/Value.h"
#include "Json/Json.h"

#include <cstring>
#include <sstream>
#include <cmath>
#include <limits>



namespace ne::json
{
	namespace internal
	{
		ne::bool_t StringCheck(ne::lpcstr_t _string, size_t _count)
		{
			if (_count == 0) return false;

			while (_count-- > 0)
			{
				if (*(_string++) == 0) return false;
			}

			return true;
		}
	}



	bool_t Value::AsBool() const
	{
		assert(IsBool());
		return *std::get_if<bool_t>(&value);
	}

	// 정수는 부호 있는/없는 64비트 두 형태로 저장되므로(2^63 이상을 담기 위해) 접근자는 양쪽을 본다.
	longlong_t Value::AsInt64() const
	{
		assert(IsInteger());

		if (const auto* signedValue = std::get_if<longlong_t>(&value)) return *signedValue;

		const auto* unsignedValue = std::get_if<ulonglong_t>(&value);
		assert(unsignedValue != nullptr);
		assert(*unsignedValue <= static_cast<ulonglong_t>(std::numeric_limits<longlong_t>::max()) && "값이 부호 있는 64비트 범위를 넘는다 — AsUint64() 를 쓸 것");

		return static_cast<longlong_t>(*unsignedValue);
	}

	ulonglong_t Value::AsUint64() const
	{
		assert(IsInteger());

		if (const auto* unsignedValue = std::get_if<ulonglong_t>(&value)) return *unsignedValue;

		const auto* signedValue = std::get_if<longlong_t>(&value);
		assert(signedValue != nullptr);
		assert(*signedValue >= 0 && "음수를 부호 없는 정수로 읽을 수 없다 — AsInt64() 를 쓸 것");

		return static_cast<ulonglong_t>(*signedValue);
	}

	double_t Value::AsDouble() const
	{
		assert(IsNumber());

		if (const auto* realValue = std::get_if<double_t>(&value)) return *realValue;
		if (const auto* signedValue = std::get_if<longlong_t>(&value)) return static_cast<double_t>(*signedValue);

		const auto* unsignedValue = std::get_if<ulonglong_t>(&value);
		assert(unsignedValue != nullptr);

		return static_cast<double_t>(*unsignedValue);
	}

	string_t* Value::AsString() const
	{
		assert(IsString());
		return std::get_if<std::shared_ptr<string_t>>(&value)->get();
	}

	const Object& Value::AsObject() const
	{
		assert(IsObject());
		return *std::get_if<Object>(&value);
	}

	const Array& Value::AsArray() const
	{
		assert(IsArray());
		return *std::get_if<Array>(&value);
	}


	// TryAs* 는 As* 와 같은 폭 변환을 하지만, 위반을 assert 가 아니라 Err 로 알린다.
	ne::Result<longlong_t> Value::TryAsInt64() const
	{
		using R = ne::Result<longlong_t>;

		if (const auto* signedValue = std::get_if<longlong_t>(&value)) return R::Ok(*signedValue);

		if (const auto* unsignedValue = std::get_if<ulonglong_t>(&value))
		{
			if (*unsignedValue > static_cast<ulonglong_t>(std::numeric_limits<longlong_t>::max())) return R::Error(ne::Error{ "json: integer exceeds signed 64-bit range" });

			return R::Ok(static_cast<longlong_t>(*unsignedValue));
		}

		return R::Error(ne::Error{ "json: expected integer" });
	}

	ne::Result<ulonglong_t> Value::TryAsUint64() const
	{
		using R = ne::Result<ulonglong_t>;

		if (const auto* unsignedValue = std::get_if<ulonglong_t>(&value)) return R::Ok(*unsignedValue);

		if (const auto* signedValue = std::get_if<longlong_t>(&value))
		{
			if (*signedValue < 0) return R::Error(ne::Error{ "json: integer is negative" });

			return R::Ok(static_cast<ulonglong_t>(*signedValue));
		}

		return R::Error(ne::Error{ "json: expected integer" });
	}

	ne::Result<double_t> Value::TryAsDouble() const
	{
		using R = ne::Result<double_t>;

		if (const auto* realValue = std::get_if<double_t>(&value)) return R::Ok(*realValue);
		if (const auto* signedValue = std::get_if<longlong_t>(&value)) return R::Ok(static_cast<double_t>(*signedValue));
		if (const auto* unsignedValue = std::get_if<ulonglong_t>(&value)) return R::Ok(static_cast<double_t>(*unsignedValue));

		return R::Error(ne::Error{ "json: expected number" });
	}

	ne::Result<string_t*> Value::TryAsString() const
	{
		if (const auto* p = std::get_if<std::shared_ptr<string_t>>(&value)) return ne::Result<string_t*>::Ok(p->get());
		return ne::Result<string_t*>::Error(ne::Error{ "json: expected string" });
	}

	ne::Result<const Object*> Value::TryAsObject() const
	{
		if (const auto* p = std::get_if<Object>(&value)) return ne::Result<const Object*>::Ok(p);
		return ne::Result<const Object*>::Error(ne::Error{ "json: expected object" });
	}

	ne::Result<const Array*> Value::TryAsArray() const
	{
		if (const auto* p = std::get_if<Array>(&value)) return ne::Result<const Array*>::Ok(p);
		return ne::Result<const Array*>::Error(ne::Error{ "json: expected array" });
	}



	Value& Value::operator=(bool_t _value)
	{
		type = Type::BOOLEAN;
		value = _value;

		return *this;
	}

	Value& Value::operator=(int_t _value)
	{
		type = Type::INTEGER;
		value = static_cast<longlong_t>(_value);

		return *this;
	}

	Value& Value::operator=(uint_t _value)
	{
		type = Type::INTEGER;
		value = static_cast<longlong_t>(_value);

		return *this;
	}

	Value& Value::operator=(longlong_t _value)
	{
		type = Type::INTEGER;
		value = _value;

		return *this;
	}

	Value& Value::operator=(ulonglong_t _value)
	{
		type = Type::INTEGER;
		value = _value;

		return *this;
	}

	Value& Value::operator=(double_t _value)
	{
		type = Type::REAL;
		value = _value;

		return *this;
	}

	Value& Value::operator=(lpcstr_t _value)
	{
		type = Type::STRING;
		value = std::make_shared<string_t>(_value);

		return *this;
	}

	Value& Value::operator=(const string_t& _value)
	{
		type = Type::STRING;
		value = std::make_shared<string_t>(_value);

		return *this;
	}

	Value& Value::operator=(const Object& _value)
	{
		type = Type::OBJECT;
		value = _value;

		return *this;
	}

	Value& Value::operator=(Object&& _value)
	{
		type = Type::OBJECT;
		value = std::move(_value);

		return *this;
	}

	Value& Value::operator=(const Array& _value)
	{
		type = Type::ARRAY;
		value = _value;

		return *this;
	}

	Value& Value::operator=(Array&& _value)
	{
		type = Type::ARRAY;
		value = std::move(_value);

		return *this;
	}





	Value Value::Parse(lpcstr_t* _data, const int_t _depth)
	{
		// 중첩 깊이 초과 — 무효로 실패시켜 스택 오버플로를 방지한다.
		if (_depth > MaxParseDepth) return {};

#if defined(_WIN32)
		// NULL
		if (internal::StringCheck(*_data, 4) && _strnicmp(*_data, "null", 4) == 0)
		{
			(*_data) += 4;
			return Value(Type::NULL_DATA);
		}

		// BOOLEAN
		if ((internal::StringCheck(*_data, 4) && _strnicmp(*_data, "true", 4) == 0) || (internal::StringCheck(*_data, 5) && _strnicmp(*_data, "false", 5) == 0))
		{
			bool_t isTrue = _strnicmp(*_data, "true", 4) == 0;
			(*_data) += isTrue ? 4 : 5;

			return Value(isTrue);
		}
#elif defined(IS_POSIX)
		if (StringCheck(*_data, 4) && strncasecmp(*_data, "null", 4) == 0)
		{
			(*_data) += 4;
			return Value(Type::NULL_DATA);
		} if ((StringCheck(*_data, 4) && strncasecmp(*_data, "true", 4) == 0) || (StringCheck(*_data, 5) && strncasecmp(*_data, "false", 5) == 0))
		{
			bool_t isTrue = strncasecmp(*_data, "true", 4) == 0;
			(*_data) += isTrue ? 4 : 5;

			return Value(isTrue);
		}
#endif

		// STRING
		if (**_data == '"')
		{
			string_t str;
			if (!ParseString(&(++(*_data)), str)) return {};

			return Value(str);
		}

		// NUMBER, REAL
		if (**_data == '-' || (**_data >= '0' && **_data <= '9'))
		{
			bool_t isNegative = **_data == '-';
			if (isNegative) (*_data)++;

			bool_t isOverflow = false;
			ulonglong_t number = 0;
			double_t real = 0.0;

			if (**_data == '0') (*_data)++;
			else if (**_data >= '1' && **_data <= '9') number = ParseNumber(_data, isOverflow);
			else return {};

			bool_t isReal = false;
			if (**_data == '.')
			{
				isReal = true;

				(*_data)++;
				if (**_data < '0' || **_data > '9') return {};

				real += static_cast<double_t>(number);
				real += ParseReal(_data);
			}
			if (**_data == 'E' || **_data == 'e')
			{
				if (!isReal) real = static_cast<double_t>(number);
				isReal = true;

				(*_data)++;
				bool_t isNegativeExponential = false;
				if (**_data == '-' || **_data == '+')
				{
					isNegativeExponential = **_data == '-';
					(*_data)++;
				}

				if (**_data < '0' || **_data > '9') return {};

				ulonglong_t exponential = ParseNumber(_data, isOverflow);
				for (ulonglong_t i = 0; i < exponential; i++) { real = isNegativeExponential ? (real / 10.0) : (real * 10.0); }
			}

			// 표현 범위 초과(부호 없는 64비트 초과 등)는 무효(INVALID)로 실패시킨다. 상위 배열/객체
			// 파서는 INVALID 자식을 추가하지 않고 건너뛴다(다른 파싱 실패와 동일한 관대한 처리).
			if (isOverflow) return {};

			if (isReal) return Value((isNegative) ? real * -1 : real);

			// 정수는 폭에 따라 좁은 타입을 고르지 않는다 — 타입 태그가 INTEGER 하나이므로 저장은
			// 부호 있는 64비트를 기본으로 하고, 그 범위를 넘는 양수만 부호 없는 64비트로 담는다.
			if (isNegative)
			{
				// -(2^63) 은 longlong_t 로 표현 가능하지만 number * -1 로는 오버플로하므로 별도로 만든다.
				if (number == static_cast<ulonglong_t>(std::numeric_limits<longlong_t>::max()) + 1) return Value(std::numeric_limits<longlong_t>::min());
				if (number <= static_cast<ulonglong_t>(std::numeric_limits<longlong_t>::max())) return Value(-static_cast<longlong_t>(number));

				return {}; // 부호 있는 64비트 미만 — 표현 불가
			}

			if (number <= static_cast<ulonglong_t>(std::numeric_limits<longlong_t>::max())) return Value(static_cast<longlong_t>(number));

			return Value(number); // 2^63 이상은 부호 없는 64비트로만 담긴다
		}

		if (**_data == '[')
		{
			Array array;

			(*_data)++;
			while (**_data != 0)
			{
				if (!internal::SkipWhitespace(_data)) return {};

				if (array.empty() && **_data == ']')
				{
					(*_data)++;
					return Value(array);
				}

				auto data = Parse(_data, _depth + 1);
				if (!data.IsInvalid()) array.push_back(data);

				if (!internal::SkipWhitespace(_data)) return {};

				if (**_data == ']')
				{
					(*_data)++;
					return Value(array);
				}

				if (**_data != ',') return {};

				(*_data)++;
			}

			return {};
		}

		if (**_data == '{')
		{
			Object object;

			(*_data)++;
			while (**_data != 0)
			{
				if (!internal::SkipWhitespace(_data)) return {};

				if (object.empty() && **_data == '}')
				{
					(*_data)++;
					return Value(object);
				}

				string_t name;
				if (!ParseString(&(++(*_data)), name)) return {};

				if (!internal::SkipWhitespace(_data)) return {};
				if (*((*_data)++) != ':') return {};
				if (!internal::SkipWhitespace(_data)) return {};

				auto data = Parse(_data, _depth + 1);
				if (!data.IsInvalid()) object[name] = data;

				if (!internal::SkipWhitespace(_data)) return {};

				if (**_data == '}')
				{
					(*_data)++;
					return Value(object);
				}

				if (**_data != ',') return {};

				(*_data)++;
			}

			return {};
		}

		return {};
	}

	ulonglong_t Value::ParseNumber(lpcstr_t* _data, bool_t& _isOverflow)
	{
		ulonglong_t integer = 0;
		while (**_data != 0 && **_data >= '0' && **_data <= '9')
		{
			ulonglong_t digit = *(*_data)++ - '0';
			if (integer > (std::numeric_limits<ulonglong_t>::max() - digit) / 10)
			{
				_isOverflow = true;
				return 0;
			}

			integer = integer * 10 + digit;
		}

		return integer;
	}

	double_t Value::ParseReal(lpcstr_t* _data)
	{
		double_t decimal = 0.0;
		double_t factor = 0.1;
		while (**_data != 0 && **_data >= '0' && **_data <= '9')
		{
			int_t digit = (*(*_data)++ - '0');
			decimal += digit * factor;
			factor *= 0.1;
		}

		return decimal;
	}

	bool_t Value::ParseString(lpcstr_t* _data, string_t& _str)
	{
		_str = "";

		while (**_data != 0)
		{
			char_t c = **_data;
			if (c == '\\')
			{
				(*_data)++;

				switch (**_data)
				{
					case '"':
						c = '"';
						break;
					case '\\':
						c = '\\';
						break;
					case '/':
						c = '/';
						break;
					case 'b':
						c = '\b';
						break;
					case 'f':
						c = '\f';
						break;
					case 'n':
						c = '\n';
						break;
					case 'r':
						c = '\r';
						break;
					case 't':
						c = '\t';
						break;
					case 'u':
					{
						if (!internal::StringCheck(*_data, 5)) return false;

						uint32_t codepoint = 0;
						for (int_t i = 0; i < 4; i++)
						{
							(*_data)++;
							codepoint <<= 4;
							if (**_data >= '0' && **_data <= '9') codepoint |= (**_data - '0');
							else if (**_data >= 'A' && **_data <= 'F') codepoint |= 10 + (**_data - 'A');
							else if (**_data >= 'a' && **_data <= 'f') codepoint |= 10 + (**_data - 'a');
							else return false;
						}
						(*_data)++;

						if (codepoint <= 0x7F) { _str += static_cast<char_t>(codepoint); }
						else if (codepoint <= 0x7FF)
						{
							_str += static_cast<char_t>(0xC0 | (codepoint >> 6));
							_str += static_cast<char_t>(0x80 | (codepoint & 0x3F));
						}
						else
						{
							_str += static_cast<char_t>(0xE0 | (codepoint >> 12));
							_str += static_cast<char_t>(0x80 | ((codepoint >> 6) & 0x3F));
							_str += static_cast<char_t>(0x80 | (codepoint & 0x3F));
						}
						continue;
					}
					default:
						return false;
				}
			}
			else if (c == '"')
			{
				(*_data)++;
				return true;
			}
			else if (static_cast<unsigned char>(c) < 0x20 && c != '\t') { return false; }

			_str += c;
			(*_data)++;
		}

		return false;
	}



	string_t Value::Stringify(const bool_t _isPrettyPrint) const { return OnStringify(_isPrettyPrint ? 1 : 0); }



	string_t Value::Indent(size_t _depth)
	{
		_depth ? --_depth : 0;

		return string_t(_depth * 2, ' ');
	}

	string_t Value::StringifyString(const string_t& _str)
	{
		string_t result = "\"";

		for (const auto ch : _str)
		{
			const auto chr = static_cast<unsigned char>(ch);

			if (ch == '"' || ch == '\\' || ch == '/')
			{
				result += '\\';
				result += ch;
			}
			else if (ch == '\b') result += "\\b";
			else if (ch == '\f') result += "\\f";
			else if (ch == '\n') result += "\\n";
			else if (ch == '\r') result += "\\r";
			else if (ch == '\t') result += "\\t";
			else if (chr < 0x20 || chr == 0x7F)
			{
				constexpr char_t hex[] = "0123456789ABCDEF";

				result += "\\u00";
				result += hex[(chr >> 4) & 0xF];
				result += hex[chr & 0xF];
			}
			else { result += ch; }
		}

		result += "\"";

		return result;
	}

	string_t Value::OnStringify(const size_t _indentDepth) const
	{
		const size_t indentDepth1 = _indentDepth ? _indentDepth + 1 : 0;
		const string_t indentStr = Indent(_indentDepth);
		const string_t indentStr1 = Indent(indentDepth1);

		switch (type)
		{
			case Type::INVALID:
				return "";
			case Type::NULL_DATA:
				return "null";
			case Type::BOOLEAN:
				return AsBool() ? "true" : "false";
			case Type::STRING:
				return StringifyString(*AsString());
			case Type::INTEGER:
				// 저장 형태(부호 있는/없는)에 따라 그대로 직렬화한다 — AsInt64() 로 통일하면 2^63 이상이 깨진다.
				if (const auto* unsignedValue = std::get_if<ulonglong_t>(&value)) return std::to_string(*unsignedValue);
				return std::to_string(AsInt64());
			case Type::REAL:
			{
				const double_t real = AsDouble();

				// JSON 에는 Infinity/NaN 표기가 없다(RFC 8259) — null 로 대체한다.
				if (std::isinf(real) || std::isnan(real)) return "null";

				std::stringstream ss;
				ss.precision(15);
				ss << real;
				return ss.str();
			}
			case Type::ARRAY:
			{
				auto result = _indentDepth ? "[\n" + indentStr1 : "[";

				auto& array = AsArray();
				for (auto iter = array.begin(); iter != array.end();)
				{
					result += iter->OnStringify(indentDepth1);

					if (++iter != array.end()) result += ",";
				}
				result += _indentDepth ? "\n" + indentStr + "]" : "]";

				return result;
			}
			case Type::OBJECT:
			{
				auto result = _indentDepth ? "{\n" + indentStr1 : "{";

				auto& object = AsObject();
				for (auto iter = object.begin(); iter != object.end();)
				{
					result += StringifyString(iter->first);
					result += ":";
					result += iter->second.OnStringify(indentDepth1);

					if (++iter != object.end()) result += ",";
				}
				result += _indentDepth ? "\n" + indentStr + "}" : "}";

				return result;
			}
		}

		return "";
	}
}
