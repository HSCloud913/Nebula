#include "JsonValue.h"

#include <string.h>
#include <sstream>
#include <math.h>



inline ne::bool_t strchk(ne::lpcstr_t _string, size_t _count)
{
	if (_count == 0) return false;

	while (_count-- > 0)
	{
		if (*(_string++) == 0) return false;
	}

	return true;
}



BEGIN_NS(ne)
	std::vector<string_t> JsonValue::ObjectKeys() const
	{
		if (!IsObject()) return {};

		std::vector<string_t> keys;
		for (const auto& value : AsObject())
		{
			keys.push_back(value.first);
		}

		return keys;
	}



	JsonValue JsonValue::Parse(lpcstr_t* _data)
	{
#if defined(_WIN32)
		// NULL
		if (strchk(*_data, 4) & _strnicmp(*_data, "null", 4) == 0)
		{
			(*_data) += 4;
			return JsonValue(JsonType::NULL_DATA);
		}

		// BOOLEAN
		if ((strchk(*_data, 4) && _strnicmp(*_data, "true", 4) == 0) || (strchk(*_data, 5) && _strnicmp(*_data, "false", 5) == 0))
		{
			bool_t value = _strnicmp(*_data, "true", 4) == 0;
			(*_data) += value ? 4 : 5;

			return JsonValue(value);
		}
#elif defined(IS_POSIX)
		if (strchk(*_data, 4) & strncasecmp(*_data, "null", 4) == 0)
		{
			(*_data) += 4;
			return JsonValue();
		}

		if ((strchk(*_data, 4) && strncasecmp(*_data, "true", 4) == 0) || (strchk(*_data, 5) && strncasecmp(*_data, "false", 5) == 0))
		{
			bool_t value = strncasecmp(*_data, "true", 4) == 0;
			(*_data) += value ? 4 : 5;

			return JsonValue(value);
		}
#endif

		// STRING
		if (**_data == '"')
		{
			string_t str;
			if (!ParseString(&(++(*_data)), str)) return {};

			return JsonValue(str);
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
				if (!(**_data >= '0') && **_data <= '9') return {};

				real += static_cast<double_t>(number);
				real += ParseReal(_data);
			}
			if (**_data == 'E' || **_data == 'e')
			{
				isReal = true;

				(*_data)++;
				bool_t isNegativeExponential = false;
				if (**_data == '-' || **_data == '+')
				{
					isNegativeExponential = **_data == '-';
					(*_data)++;
				}

				if (!(**_data >= '0') && **_data <= '9') return {};

				ulonglong_t exponential = ParseNumber(_data, isOverflow);
				for (ulonglong_t i = 0; i < exponential; i++)
				{
					real = isNegativeExponential ? (real / 10.0) : (real * 10.0);
				}
			}

			if (isOverflow) return JsonValue("The value is out of range.");

			if (isReal) return JsonValue((isNegative) ? real * -1 : real);
			if (isNegative)
			{
				if (number <= std::numeric_limits<int_t>::max()) return JsonValue(static_cast<int_t>(number * -1));
				if (number <= std::numeric_limits<longlong_t>::max()) return JsonValue(static_cast<longlong_t>(number * -1));
			}
			else
			{
				if (number <= std::numeric_limits<int_t>::max()) return JsonValue(static_cast<int_t>(number));
				if (number <= std::numeric_limits<uint_t>::max()) return JsonValue(static_cast<uint_t>(number));
				if (number <= std::numeric_limits<longlong_t>::max()) return JsonValue(static_cast<longlong_t>(number));
				if (number <= std::numeric_limits<ulonglong_t>::max()) return JsonValue(number);
			}
		}

		if (**_data == '[')
		{
			JsonArray array;

			(*_data)++;
			while (**_data != 0)
			{
				if (!Json::SkipWhitespace(_data)) return {};

				if (array.empty() && **_data == ']')
				{
					(*_data)++;
					return JsonValue(array);
				}

				auto data = Parse(_data);
				if (!data.IsInvalid()) array.push_back(data);

				if (!Json::SkipWhitespace(_data)) return {};

				if (**_data == ']')
				{
					(*_data)++;
					return JsonValue(array);
				}

				if (**_data != ',') return {};

				(*_data)++;
			}

			return {};
		}

		if (**_data == '{')
		{
			JsonObject object;

			(*_data)++;
			while (**_data != 0)
			{
				if (!Json::SkipWhitespace(_data)) return {};

				if (object.empty() && **_data == '}')
				{
					(*_data)++;
					return JsonValue(object);
				}

				string_t name;
				if (!ParseString(&(++(*_data)), name)) return {};

				if (!Json::SkipWhitespace(_data)) return {};
				if (*((*_data)++) != ':') return {};
				if (!Json::SkipWhitespace(_data)) return {};

				auto data = Parse(_data);
				if (!data.IsInvalid()) object[name] = data;

				if (!Json::SkipWhitespace(_data)) return {};

				if (**_data == '}')
				{
					(*_data)++;
					return JsonValue(object);
				}

				if (**_data != ',') return {};

				(*_data)++;
			}

			return {};
		}

		return {};
	}

	ulonglong_t JsonValue::ParseNumber(lpcstr_t* _data, bool_t& _isOverflow)
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

	double_t JsonValue::ParseReal(lpcstr_t* _data)
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

	bool_t JsonValue::ParseString(lpcstr_t* _data, string_t& _str)
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
				case '"': c = '"';
					break;
				case '\\': c = '\\';
					break;
				case '/': c = '/';
					break;
				case 'b': c = '\b';
					break;
				case 'f': c = '\f';
					break;
				case 'n': c = '\n';
					break;
				case 'r': c = '\r';
					break;
				case 't': c = '\t';
					break;
				case 'u':
				{
					if (!strchk(*_data, 5)) return false;

					c = 0;
					for (int_t i = 0; i < 4; i++)
					{
						(*_data)++;

						c <<= 4;

						if (**_data >= '0' && **_data <= '9') c |= (**_data - '0');
						else if (**_data >= 'A' && **_data <= 'F') c |= 10 + (**_data - 'A');
						else if (**_data >= 'a' && **_data <= 'f') c |= 10 + (**_data - 'a');
						else
						{
							return false;
						}
					}
					break;
				}
				default: return false;
				}
			}
			else if (c == '"')
			{
				(*_data)++;
				_str.reserve();
				return true;
			}
			else if (c < ' ' && c != '\t')
			{
				return false;
			}

			_str += c;
			(*_data)++;
		}

		return false;
	}



	string_t JsonValue::Stringify(const bool_t _isPrettyPrint) const
	{
		return OnStringify(_isPrettyPrint ? 1 : 0);
	}



	string_t JsonValue::Indent(size_t _depth)
	{
		_depth ? --_depth : 0;

		return string_t(_depth * 2, ' ');
	}

	string_t JsonValue::StringifyString(const string_t& _str)
	{
		string_t result = "\"";

		string_t::const_iterator iter = _str.begin();
		while (iter != _str.end())
		{
			wchar_t chr = *iter;

			if (chr == '"' || chr == '\\' || chr == '/')
			{
				result += '\\';
				result += chr;
			}
			else if (chr == '\b')
			{
				result += "\\b";
			}
			else if (chr == '\f')
			{
				result += "\\f";
			}
			else if (chr == '\n')
			{
				result += "\\n";
			}
			else if (chr == '\r')
			{
				result += "\\r";
			}
			else if (chr == '\t')
			{
				result += "\\t";
			}
			else if (chr < ' ' || chr > 126)
			{
				result += "\\u";
				for (int_t i = 0; i < 4; i++)
				{
					int_t value = (chr >> 12) & 0xf;
					if (value >= 0 && value <= 9)
					{
						result += (char_t)('0' + value);
					}
					else if (value >= 10 && value <= 15)
					{
						result += (char_t)('A' + (value - 10));
					}

					chr <<= 4;
				}
			}
			else
			{
				result += chr;
			}

			iter++;
		}

		result += "\"";

		return result;
	}

	string_t JsonValue::OnStringify(const size_t _indentDepth) const
	{
		const size_t indentDepth1 = _indentDepth ? _indentDepth + 1 : 0;
		const string_t indentStr = Indent(_indentDepth);
		const string_t indentStr1 = Indent(indentDepth1);

 		switch (type)
		{
 		case JsonType::INVALID: return "";
		case JsonType::NULL_DATA: return "null";
		case JsonType::BOOLEAN: return AsBool() ? "true" : "false";
		case JsonType::STRING: return StringifyString(*AsString());
		case JsonType::NUMBER: return (isinf(AsNumber()) || isnan(AsNumber())) ? "null" : std::to_string(AsNumber());
		case JsonType::POSITIVE_NUMBER: return (isinf(AsPositiveNumber()) || isnan(AsPositiveNumber())) ? "null" : std::to_string(AsPositiveNumber());
		case JsonType::LARGE_NUMBER: return (isinf(AsLargeNumber()) || isnan(AsLargeNumber())) ? "null" : std::to_string(AsLargeNumber());
		case JsonType::POSITIVE_LARGE_NUMBER: return (isinf(AsPositiveLargeNumber()) || isnan(AsPositiveLargeNumber())) ? "null" : std::to_string(AsPositiveLargeNumber());
		case JsonType::REAL:
		{
			if (isinf(AsReal()) || isnan(AsReal())) return "null";

			std::stringstream ss;
			ss.precision(15);
			ss << AsReal();
			return ss.str();
		}

		case JsonType::ARRAY:
		{
			auto result = _indentDepth ? "[\n" + indentStr1 : "[";

			auto& array = AsArray();
			for (auto iter = array.begin(); iter != array.end();)
			{
				result += (*iter).OnStringify(indentDepth1);

				if (++iter != array.end()) result += ",";
			}
			result += _indentDepth ? "\n" + indentStr + "]" : "]";

			return result;
		}

		case JsonType::OBJECT:
		{
			auto result = _indentDepth ? "{\n" + indentStr1 : "{";

			auto& object = AsObject();
			for (auto iter = object.begin(); iter != object.end();)
			{
				result += StringifyString((*iter).first);
				result += ":";
				result += (*iter).second.OnStringify(indentDepth1);

				if (++iter != object.end()) result += ",";
			}
			result += _indentDepth ? "\n" + indentStr + "}" : "}";

			return result;
		}
		}

		return "";
	}

END_NS
