#include "Json.h"



BEGIN_NS(ne)
	JsonValue Json::Parse(lpcstr_t _data)
	{
		if (!SkipWhitespace(&_data)) return {};

		auto value = JsonValue::Parse(&_data);
		if (value.IsNull()) return {};

		if (SkipWhitespace(&_data)) return {};

		return value;
	}

	string_t Json::Stringify(const JsonValue& _value)
	{
		if (!_value.IsNull()) return _value.Stringify();

		return "";
	}

	string_t Json::Stringify(const JsonObject& _value)
	{
		return JsonValue(_value).Stringify();
	}

	string_t Json::Stringify(const JsonArray& _value)
	{
		return JsonValue(_value).Stringify();
	}



	bool_t Json::SkipWhitespace(lpcstr_t* _data)
	{
		while (**_data != 0 && (**_data == ' ' || **_data == '\t' || **_data == '\r' || **_data == '\n'))
		{
			(*_data)++;
		}

		return **_data != 0;
	}

END_NS
