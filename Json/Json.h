#pragma once
#include <string>
#include "Base/Type.h"

#include "Json/JsonValue.h"

BEGIN_NS(ne)
	/**
	 * @class Json
	 * @brief JSON 문자열 파싱과 JsonValue/JsonObject/JsonArray 직렬화를 위한 정적 진입점입니다.
	 *
	 * 인스턴스화하지 않고 Parse()/Stringify() 정적 메서드만 사용합니다.
	 */
	class Json final
	{
		friend class JsonValue;

	private:
		Json() = default;

	public:
		/** @brief 문자열을 파싱해 JsonValue 로 반환합니다. 파싱 실패 또는 후행 데이터가 남으면 무효(INVALID) 값을 반환합니다. */
		static JsonValue Parse(lpcstr_t _data);

		// JsonValue/JsonObject/JsonArray 를 각각 JSON 문자열로 직렬화한다.
		static string_t Stringify(const JsonValue& _value) { return _value.IsInvalid() ? "" : _value.Stringify(); }
		static string_t Stringify(const JsonObject& _value) { return JsonValue(_value).Stringify(); }
		static string_t Stringify(const JsonArray& _value) { return JsonValue(_value).Stringify(); }

	private:
		/** @brief 공백을 건너뛰고, 그 뒤에 파싱할 데이터가 더 남아있는지(널 종단이 아닌지) 반환합니다. */
		static bool_t SkipWhitespace(lpcstr_t* _data);
	};

END_NS
