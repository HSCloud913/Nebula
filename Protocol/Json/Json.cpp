#include "Json.h"



BEGIN_NS(ne::protocol)
	std::shared_ptr<JsonValue> Json::Parse(lpcstr_t _data)
	{
		if (!SkipWhitespace(&_data)) return nullptr;

		auto value = JsonValue::Parse(&_data);
		if (value == nullptr) return nullptr;

		if (SkipWhitespace(&_data))
		{
			//delete value;
			return nullptr;
		}

		return value;
	}

	string_t Json::Stringify(const std::shared_ptr<JsonValue>& _value)
	{
		if (_value != nullptr) return _value->Stringify();

		return "";
	}

	string_t Json::Stringify(const JsonObject& _value)
	{
		return std::make_shared<JsonValue>(_value)->Stringify();
	}

	string_t Json::Stringify(const JsonArray& _value)
	{
		return std::make_shared<JsonValue>(_value)->Stringify();
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
