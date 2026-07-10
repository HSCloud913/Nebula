#include "Json/Json.h"



BEGIN_NS(ne)
	JsonValue Json::Parse(lpcstr_t _data)
	{
		if (!SkipWhitespace(&_data)) return {};

		auto value = JsonValue::Parse(&_data);
		if (value.IsInvalid()) return {};

		if (SkipWhitespace(&_data)) return {};

		return value;
	}



	bool_t Json::SkipWhitespace(lpcstr_t* _data)
	{
		while (**_data != 0 && (**_data == ' ' || **_data == '\t' || **_data == '\r' || **_data == '\n')) { (*_data)++; }

		return **_data != 0;
	}

END_NS
