#pragma once
#include <cassert>
#include <cstddef>
#include <map>
#include <memory>
#include <variant>
#include <vector>
#include "Base/Type.h"
#include "Base/Result.h"

namespace ne::json
{
	class Value;
	typedef std::map<string_t, Value> Object;
	typedef std::vector<Value> Array;

	enum class Type
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

	/**
	 * @struct ParseError
	 * @brief TryParse() 실패 정보. position 은 실패한 바이트 오프셋, message 는 사유입니다.
	 */
	struct ParseError
	{
		std::size_t position{};
		string_t message;
	};

	/**
	 * @class Value
	 * @brief JSON 값 하나(null/bool/숫자/문자열/객체/배열)를 담는 variant 기반 래퍼입니다.
	 *
	 * 실제 타입은 Type 으로 구분되며, 숫자는 부호/크기별로 NUMBER/POSITIVE_NUMBER/
	 * LARGE_NUMBER/POSITIVE_LARGE_NUMBER/REAL 다섯 종류로 세분화되어 있습니다. 타입이 확실할 때는
	 * Is*() 확인 후 As*() 로 값을 꺼내고(무예외; 선행 조건 위반 시 assert), 타입을 모를 때는
	 * TryAs*() 가 ne::Result 로 안전하게 반환합니다.
	 */
	class Value final
	{
		friend ne::Result<Value, ParseError> TryParse(lpcstr_t _data);

	public:
		Value()
			: type(Type::INVALID) {}

		explicit Value(const Type _type)
			: type(_type) {}

		explicit Value(const bool_t _value)
			: type(Type::BOOLEAN)
			, value(_value) {}

		explicit Value(const int_t _value)
			: type(Type::NUMBER)
			, value(_value) {}

		explicit Value(const uint_t _value)
			: type(Type::POSITIVE_NUMBER)
			, value(_value) {}

		explicit Value(const longlong_t _value)
			: type(Type::LARGE_NUMBER)
			, value(_value) {}

		explicit Value(const ulonglong_t _value)
			: type(Type::POSITIVE_LARGE_NUMBER)
			, value(_value) {}

		explicit Value(const double_t _value)
			: type(Type::REAL)
			, value(_value) {}

		explicit Value(lpcstr_t _value)
			: type(Type::STRING)
			, value(std::make_shared<string_t>(_value)) {}

		explicit Value(const string_t& _value)
			: type(Type::STRING)
			, value(std::make_shared<string_t>(_value)) {}

		explicit Value(const Object& _value)
			: type(Type::OBJECT)
			, value(_value) {}

		explicit Value(Object&& _value)
			: type(Type::OBJECT)
			, value(std::move(_value)) {}

		explicit Value(const Array& _value)
			: type(Type::ARRAY)
			, value(_value) {}

		explicit Value(Array&& _value)
			: type(Type::ARRAY)
			, value(std::move(_value)) {}

		Value(const Value& _value)
			: type(_value.type)
			, value(_value.value) {}

	private:
		// 신뢰할 수 없는 입력의 깊은 중첩(예: "[[[[…]]]]")이 무한 재귀→스택 오버플로를 내지 않도록
		// 중첩 깊이를 제한한다. 초과 시 무효(INVALID) Value 로 실패시킨다.
		static constexpr int_t MaxParseDepth = 256;

		using Storage = std::variant<std::monostate, bool_t, int_t, uint_t, longlong_t, ulonglong_t, double_t, std::shared_ptr<string_t>, Object, Array>;

	private:
		Type type;
		Storage value;

	public:
		[[nodiscard]] bool_t IsInvalid() const { return type == Type::INVALID; }
		[[nodiscard]] bool_t IsNull() const { return type == Type::NULL_DATA; }
		[[nodiscard]] bool_t IsBool() const { return type == Type::BOOLEAN; }
		[[nodiscard]] bool_t IsNumber() const { return type == Type::NUMBER; }
		[[nodiscard]] bool_t IsPositiveNumber() const { return type == Type::POSITIVE_NUMBER; }
		[[nodiscard]] bool_t IsLargeNumber() const { return type == Type::LARGE_NUMBER; }
		[[nodiscard]] bool_t IsPositiveLargeNumber() const { return type == Type::POSITIVE_LARGE_NUMBER; }
		[[nodiscard]] bool_t IsReal() const { return type == Type::REAL; }
		[[nodiscard]] bool_t IsString() const { return type == Type::STRING; }
		[[nodiscard]] bool_t IsObject() const { return type == Type::OBJECT; }
		[[nodiscard]] bool_t IsArray() const { return type == Type::ARRAY; }

		// [무예외] 타입이 확실할 때(Is*() 선행 확인 후) 쓰는 빠른 접근자. 선행 조건 위반은
		// 프로그래머 오류로 간주해 assert 로 잡는다(릴리스에서는 UB) — ne::Result::Value() 와 동일 계약.
		// 타입을 모르는 상태에서의 안전한 접근은 아래 TryAs*() (Result 반환)를 사용한다.
		[[nodiscard]] bool_t AsBool() const;
		[[nodiscard]] int_t AsNumber() const;
		[[nodiscard]] uint_t AsPositiveNumber() const;
		[[nodiscard]] longlong_t AsLargeNumber() const;
		[[nodiscard]] ulonglong_t AsPositiveLargeNumber() const;
		[[nodiscard]] double_t AsReal() const;
		[[nodiscard]] string_t* AsString() const;
		[[nodiscard]] const Object& AsObject() const;
		[[nodiscard]] const Array& AsArray() const;

		// [안전] 타입 불일치 시 예외 대신 Err 를 반환한다("예외 없음" 철학). 참조 타입은 Result 에
		// 담을 수 없어 Object/Array/String 은 포인터로 반환한다(성공 시 non-null).
		[[nodiscard]] ne::Result<bool_t> TryAsBool() const { return AsScalar<bool_t>("json: expected bool"); }
		[[nodiscard]] ne::Result<int_t> TryAsNumber() const { return AsScalar<int_t>("json: expected number"); }
		[[nodiscard]] ne::Result<uint_t> TryAsPositiveNumber() const { return AsScalar<uint_t>("json: expected positive number"); }
		[[nodiscard]] ne::Result<longlong_t> TryAsLargeNumber() const { return AsScalar<longlong_t>("json: expected large number"); }
		[[nodiscard]] ne::Result<ulonglong_t> TryAsPositiveLargeNumber() const { return AsScalar<ulonglong_t>("json: expected positive large number"); }
		[[nodiscard]] ne::Result<double_t> TryAsReal() const { return AsScalar<double_t>("json: expected real"); }
		[[nodiscard]] ne::Result<string_t*> TryAsString() const;
		[[nodiscard]] ne::Result<const Object*> TryAsObject() const;
		[[nodiscard]] ne::Result<const Array*> TryAsArray() const;

	public:
		Value& operator=(bool_t _value);
		Value& operator=(int_t _value);
		Value& operator=(uint_t _value);
		Value& operator=(longlong_t _value);
		Value& operator=(ulonglong_t _value);
		Value& operator=(double_t _value);
		Value& operator=(lpcstr_t _value);
		Value& operator=(const string_t& _value);
		Value& operator=(const Object& _value);
		Value& operator=(Object&& _value);
		Value& operator=(const Array& _value);
		Value& operator=(Array&& _value);

	protected:
		/** @brief 재귀 하강 파서의 진입점입니다. _depth 가 MaxParseDepth 를 넘으면 무효 값으로 실패시킵니다. */
		static Value Parse(lpcstr_t* _data, int_t _depth = 0);
		/** @brief 부호 없는 정수부 숫자를 파싱합니다. 오버플로 시 _isOverflow 를 true 로 세팅합니다. */
		static ulonglong_t ParseNumber(lpcstr_t* _data, bool_t& _isOverflow);
		/** @brief 소수점 이하 자릿수를 0.xxx 형태의 실수로 파싱합니다. */
		static double_t ParseReal(lpcstr_t* _data);
		/** @brief 여는 큰따옴표 다음부터 닫는 큰따옴표까지 이스케이프(\\uXXXX 포함)를 해석해 문자열로 파싱합니다. */
		static bool_t ParseString(lpcstr_t* _data, string_t& _str);

	public:
		/** @brief 이 값을 JSON 문자열로 직렬화합니다. _isPrettyPrint 가 true 면 들여쓰기와 줄바꿈을 포함합니다. */
		[[nodiscard]] string_t Stringify(bool_t _isPrettyPrint = false) const;

	private:
		/** @brief TryAs*() 스칼라 구현: 타입 일치 시 Ok(값), 불일치 시 Err(_message). */
		template <typename T>
		[[nodiscard]] ne::Result<T> AsScalar(lpcstr_t _message) const
		{
			if (const auto* p = std::get_if<T>(&value)) return ne::Result<T>::Ok(*p);
			return ne::Result<T>::Error(ne::Error{ _message });
		}
		/** @brief 들여쓰기 깊이에 해당하는 공백 문자열을 만듭니다. */
		static string_t Indent(size_t _depth);
		/** @brief 제어문자/특수문자를 이스케이프 처리해 문자열을 JSON 문자열 리터럴로 만듭니다. */
		static string_t StringifyString(const string_t& _str);
		/** @brief Stringify() 의 재귀 구현체입니다. _indentDepth 가 0이면 압축, 0보다 크면 들여쓰기 출력을 합니다. */
		[[nodiscard]] string_t OnStringify(size_t _indentDepth) const;
	};
}
