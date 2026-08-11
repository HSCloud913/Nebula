#pragma once
#include <string>
#include "Base/Type.h"

#include "Json/Value.h"

namespace ne::json
{
	namespace internal
	{
		/** @brief 공백을 건너뛰고, 그 뒤에 파싱할 데이터가 더 남아있는지(널 종단이 아닌지) 반환합니다. */
		bool_t SkipWhitespace(lpcstr_t* _data);
	}

	/** @brief JSON 문자열을 파싱해 Value 로 반환합니다. 파싱 실패 또는 후행 데이터가 남으면 무효(INVALID) 값을 반환합니다. */
	[[nodiscard]] Value Parse(lpcstr_t _data);

	/** @brief Parse 의 checked 버전: 실패 시 위치(오프셋)와 사유를 담은 ParseError 를 Err 로 반환합니다. */
	[[nodiscard]] ne::Result<Value, ParseError> TryParse(lpcstr_t _data);

	/** @brief Value/Object/Array 를 각각 JSON 문자열로 직렬화합니다. */
	[[nodiscard]] inline string_t Stringify(const Value& _value) { return _value.IsInvalid() ? "" : _value.Stringify(); }
	[[nodiscard]] inline string_t Stringify(const Object& _value) { return Value(_value).Stringify(); }
	[[nodiscard]] inline string_t Stringify(const Array& _value) { return Value(_value).Stringify(); }
}
