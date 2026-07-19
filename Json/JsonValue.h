#pragma once
#include <map>
#include <memory>
#include <variant>
#include <vector>
#include "Base/Type.h"

BEGIN_NS(ne)
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

	/**
	 * @class JsonValue
	 * @brief JSON 값 하나(null/bool/숫자/문자열/객체/배열)를 담는 variant 기반 래퍼입니다.
	 *
	 * 실제 타입은 JsonType 으로 구분되며, 숫자는 부호/크기별로 NUMBER/POSITIVE_NUMBER/
	 * LARGE_NUMBER/POSITIVE_LARGE_NUMBER/REAL 다섯 종류로 세분화되어 있습니다. Is*() 로
	 * 현재 타입을 확인한 뒤 As*() 로 값을 꺼내며, 타입이 다르면 std::bad_variant_access 가
	 * 발생합니다.
	 */
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
		/** @brief 이 값이 객체(OBJECT)일 때 키 목록을 반환합니다. 객체가 아니면 빈 벡터를 반환합니다. */
		[[nodiscard]] std::vector<string_t> ObjectKeys() const;

	protected:
		// 신뢰할 수 없는 입력의 깊은 중첩(예: "[[[[…]]]]")이 무한 재귀→스택 오버플로를 내지 않도록
		// 중첩 깊이를 제한한다. 초과 시 무효(INVALID) JsonValue 로 실패시킨다.
		static constexpr int_t MaxParseDepth = 256;

		/** @brief 재귀 하강 파서의 진입점입니다. _depth 가 MaxParseDepth 를 넘으면 무효 값으로 실패시킵니다. */
		static JsonValue Parse(lpcstr_t* _data, int_t _depth = 0);

		/** @brief 부호 없는 정수부 숫자를 파싱합니다. 오버플로 시 _isOverflow 를 true 로 세팅합니다. */
		static ulonglong_t ParseNumber(lpcstr_t* _data, bool_t& _isOverflow);

		/** @brief 소수점 이하 자릿수를 0.xxx 형태의 실수로 파싱합니다. */
		static double_t ParseReal(lpcstr_t* _data);

		/** @brief 여는 큰따옴표 다음부터 닫는 큰따옴표까지 이스케이프(\\uXXXX 포함)를 해석해 문자열로 파싱합니다. */
		static bool_t ParseString(lpcstr_t* _data, string_t& _str);

	public:
		/** @brief 이 값을 JSON 문자열로 직렬화합니다. _isPrettyPrint 가 true 면 들여쓰기와 줄바꿈을 포함합니다. */
		[[nodiscard]] string_t Stringify(const bool_t _isPrettyPrint = false) const;

	private:
		/** @brief 들여쓰기 깊이에 해당하는 공백 문자열을 만듭니다. */
		static string_t Indent(size_t _depth);

		/** @brief 제어문자/특수문자를 이스케이프 처리해 문자열을 JSON 문자열 리터럴로 만듭니다. */
		static string_t StringifyString(const string_t& _str);

		/** @brief Stringify() 의 재귀 구현체입니다. _indentDepth 가 0이면 압축, 0보다 크면 들여쓰기 출력을 합니다. */
		[[nodiscard]] string_t OnStringify(const size_t _indentDepth) const;
	};

END_NS
