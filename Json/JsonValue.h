#pragma once
#include <map>
#include <memory>
#include <variant>
#include <vector>
#include "Base/Type.h"

BEGIN_NS(ne)
	// JsonObject/JsonArray 는 JsonValue 를 원소로 갖는다(불완전형 허용). Json.h 의 인라인 Stringify 가
	// 완전한 JsonValue 를 필요로 하므로, 순환 include 를 피하려 공용 typedef 를 여기에 둔다(Json.h 는 이 헤더를 include).
	class JsonValue;
	typedef std::map<string_t, JsonValue> JsonObject;
	typedef std::vector<JsonValue> JsonArray;

	enum class JsonType
	{
		INVALID,
		NULL_DATA,
		BOOLEAN,
		NUMBER,
		POSITIVE_NUMBER,
		LARGE_NUMBER,
		POSITIVE_LARGE_NUMBER,
		REAL,
		STRING,
		ARRAY,
		OBJECT
	};

	class Json;

	class JsonValue final
	{
		friend class Json;

	public:
		JsonValue()
			: type(JsonType::INVALID) {}

		explicit JsonValue(const JsonType _type)
			: type(_type) {}

		explicit JsonValue(const bool_t _value)
			: type(JsonType::BOOLEAN)
			, value(_value) {}

		explicit JsonValue(const int_t _value)
			: type(JsonType::NUMBER)
			, value(_value) {}

		explicit JsonValue(const uint_t _value)
			: type(JsonType::POSITIVE_NUMBER)
			, value(_value) {}

		explicit JsonValue(const longlong_t _value)
			: type(JsonType::LARGE_NUMBER)
			, value(_value) {}

		explicit JsonValue(const ulonglong_t _value)
			: type(JsonType::POSITIVE_LARGE_NUMBER)
			, value(_value) {}

		explicit JsonValue(const double_t _value)
			: type(JsonType::REAL)
			, value(_value) {}

		explicit JsonValue(lpcstr_t _value)
			: type(JsonType::STRING)
			, value(std::make_shared<string_t>(_value)) {}

		explicit JsonValue(const string_t& _value)
			: type(JsonType::STRING)
			, value(std::make_shared<string_t>(_value)) {}

		explicit JsonValue(const JsonObject& _value)
			: type(JsonType::OBJECT)
			, value(_value) {}

		explicit JsonValue(JsonObject&& _value)
			: type(JsonType::OBJECT)
			, value(std::move(_value)) {}

		explicit JsonValue(const JsonArray& _value)
			: type(JsonType::ARRAY)
			, value(_value) {}

		explicit JsonValue(JsonArray&& _value)
			: type(JsonType::ARRAY)
			, value(std::move(_value)) {}

		JsonValue(const JsonValue& _value)
			: type(_value.type)
			, value(_value.value) {}

	private:
		using Value = std::variant<std::monostate, bool_t, int_t, uint_t, longlong_t, ulonglong_t, double_t, std::shared_ptr<string_t>, JsonObject, JsonArray>;

	private:
		JsonType type;
		Value value;

	public:
		[[nodiscard]] bool_t IsInvalid() const { return type == JsonType::INVALID; }
		[[nodiscard]] bool_t IsNull() const { return type == JsonType::NULL_DATA; }
		[[nodiscard]] bool_t IsBool() const { return type == JsonType::BOOLEAN; }
		[[nodiscard]] bool_t IsNumber() const { return type == JsonType::NUMBER; }
		[[nodiscard]] bool_t IsPositiveNumber() const { return type == JsonType::POSITIVE_NUMBER; }
		[[nodiscard]] bool_t IsLargeNumber() const { return type == JsonType::LARGE_NUMBER; }
		[[nodiscard]] bool_t IsPositiveLargeNumber() const { return type == JsonType::POSITIVE_LARGE_NUMBER; }
		[[nodiscard]] bool_t IsReal() const { return type == JsonType::REAL; }
		[[nodiscard]] bool_t IsString() const { return type == JsonType::STRING; }
		[[nodiscard]] bool_t IsObject() const { return type == JsonType::OBJECT; }
		[[nodiscard]] bool_t IsArray() const { return type == JsonType::ARRAY; }

		[[nodiscard]] bool_t AsBool() const { return std::get<bool_t>(value); }
		[[nodiscard]] int_t AsNumber() const { return std::get<int_t>(value); }
		[[nodiscard]] uint_t AsPositiveNumber() const { return std::get<uint_t>(value); }
		[[nodiscard]] longlong_t AsLargeNumber() const { return std::get<longlong_t>(value); }
		[[nodiscard]] ulonglong_t AsPositiveLargeNumber() const { return std::get<ulonglong_t>(value); }
		[[nodiscard]] double_t AsReal() const { return std::get<double>(value); }
		[[nodiscard]] string_t* AsString() const { return std::get<std::shared_ptr<string_t>>(value).get(); }
		[[nodiscard]] const JsonObject& AsObject() const { return std::get<JsonObject>(value); }
		[[nodiscard]] const JsonArray& AsArray() const { return std::get<JsonArray>(value); }

		JsonValue& operator=(bool_t _value)
		{
			type = JsonType::BOOLEAN;
			value = _value;
			return *this;
		}
		JsonValue& operator=(int_t _value)
		{
			type = JsonType::NUMBER;
			value = _value;
			return *this;
		}
		JsonValue& operator=(uint_t _value)
		{
			type = JsonType::POSITIVE_NUMBER;
			value = _value;
			return *this;
		}
		JsonValue& operator=(longlong_t _value)
		{
			type = JsonType::LARGE_NUMBER;
			value = _value;
			return *this;
		}
		JsonValue& operator=(ulonglong_t _value)
		{
			type = JsonType::POSITIVE_LARGE_NUMBER;
			value = _value;
			return *this;
		}
		JsonValue& operator=(double_t _value)
		{
			type = JsonType::REAL;
			value = _value;
			return *this;
		}
		JsonValue& operator=(lpcstr_t _value)
		{
			type = JsonType::STRING;
			value = std::make_shared<string_t>(_value);
			return *this;
		}
		JsonValue& operator=(const string_t& _value)
		{
			type = JsonType::STRING;
			value = std::make_shared<string_t>(_value);
			return *this;
		}
		JsonValue& operator=(const JsonObject& _value)
		{
			type = JsonType::OBJECT;
			value = _value;
			return *this;
		}
		JsonValue& operator=(JsonObject&& _value)
		{
			type = JsonType::OBJECT;
			value = std::move(_value);
			return *this;
		}
		JsonValue& operator=(const JsonArray& _value)
		{
			type = JsonType::ARRAY;
			value = _value;
			return *this;
		}
		JsonValue& operator=(JsonArray&& _value)
		{
			type = JsonType::ARRAY;
			value = std::move(_value);
			return *this;
		}

	public:
		[[nodiscard]] std::vector<string_t> ObjectKeys() const;

	protected:
		// 신뢰할 수 없는 입력의 깊은 중첩(예: "[[[[…]]]]")이 무한 재귀→스택 오버플로를 내지 않도록
		// 중첩 깊이를 제한한다. 초과 시 무효(INVALID) JsonValue 로 실패시킨다.
		static constexpr int_t MaxParseDepth = 256;

		static JsonValue Parse(lpcstr_t* _data, int_t _depth = 0);
		static ulonglong_t ParseNumber(lpcstr_t* _data, bool_t& _isOverflow);
		static double_t ParseReal(lpcstr_t* _data);
		static bool_t ParseString(lpcstr_t* _data, string_t& _str);

	public:
		[[nodiscard]] string_t Stringify(const bool_t _isPrettyPrint = false) const;

	private:
		static string_t Indent(size_t _depth);
		static string_t StringifyString(const string_t& _str);
		[[nodiscard]] string_t OnStringify(const size_t _indentDepth) const;
	};

END_NS
