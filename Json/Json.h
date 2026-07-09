#pragma once
#include <any>
#include <vector>
#include <string>
#include <map>
#include "Base/Type.h"

#include "Json/JsonValue.h"

BEGIN_NS(ne)
	class Json final
	{
		friend class JsonValue;

	private:
		Json() = default;

	public:
		static JsonValue Parse(lpcstr_t _data);
		static string_t Stringify(const JsonValue& _value) { return _value.IsInvalid() ? "" : _value.Stringify(); }
		static string_t Stringify(const JsonObject& _value) { return JsonValue(_value).Stringify(); }
		static string_t Stringify(const JsonArray& _value) { return JsonValue(_value).Stringify(); }

	private:
		static bool_t SkipWhitespace(lpcstr_t* _data);
	};

END_NS
