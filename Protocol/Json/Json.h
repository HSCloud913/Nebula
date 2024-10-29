#ifndef JSON_H
#define JSON_H

#include <any>
#include <vector>
#include <string>
#include <memory>
#include <map>

#include "Type.h"

BEGIN_NS(ne::protocol)
	class JsonValue;

	typedef std::map<string_t, std::shared_ptr<JsonValue>> JsonObject;
	typedef std::vector<std::shared_ptr<JsonValue>> JsonArray;
END_NS

#include "JsonValue.h"

BEGIN_NS(ne::protocol)
	class Json final
	{
		friend class JsonValue;

	private:
		Json() = default;

	public:
		static std::shared_ptr<JsonValue> Parse(lpcstr_t _data);
		static string_t Stringify(const std::shared_ptr<JsonValue>& _value);
		static string_t Stringify(const JsonObject& _value);
		static string_t Stringify(const JsonArray& _value);

	private:
		static bool_t SkipWhitespace(lpcstr_t* _data);
	};

END_NS

typedef ne::protocol::Json NebulaJson;
typedef ne::protocol::JsonValue NebulaJsonValue;
typedef ne::protocol::JsonObject NebulaJsonObject;
typedef ne::protocol::JsonArray NebulaJsonArray;

#define NE_V(value) std::make_shared<ne::protocol::JsonValue>(static_cast<decltype(value)>(value))

#endif //JSON_H
