#ifndef NEBULAJSON_H
#define NEBULAJSON_H

#include <any>
#include <vector>
#include <string>
#include <memory>
#include <map>

#include "Type.h"

BEGIN_NS(ne)
	class JsonValue;
	typedef std::map<string_t, JsonValue> JsonObject;
	typedef std::vector<JsonValue> JsonArray;
END_NS

#include "JsonValue.h"

BEGIN_NS(ne)
	class Json final
	{
		friend class JsonValue;

	private:
		Json() = default;

	public:
		static JsonValue Parse(lpcstr_t _data);
		static string_t Stringify(const JsonValue& _value);
		static string_t Stringify(const JsonObject& _value);
		static string_t Stringify(const JsonArray& _value);

	private:
		static bool_t SkipWhitespace(lpcstr_t* _data);
	};

END_NS

typedef ne::Json NebulaJson;
typedef ne::JsonValue NebulaJsonValue;
typedef ne::JsonObject NebulaJsonObject;
typedef ne::JsonArray NebulaJsonArray;

#endif //NEBULAJSON_H
