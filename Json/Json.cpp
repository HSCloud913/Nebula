#include <format>
#include <utility>
#include "Json/Json.h"



namespace ne::json
{
	namespace internal
	{
		bool_t SkipWhitespace(lpcstr_t* _data)
		{
			while (**_data != 0 && (**_data == ' ' || **_data == '\t' || **_data == '\r' || **_data == '\n')) { (*_data)++; }

			return **_data != 0;
		}
	}



	ne::Result<Value, ParseError> TryParse(lpcstr_t _data)
	{
		const lpcstr_t start = _data;
		lpcstr_t cur = _data;

		// 루트에 값이 없음(빈 입력/공백만)
		if (!internal::SkipWhitespace(&cur))
			return ne::Result<Value, ParseError>::Error(ParseError{ static_cast<std::size_t>(cur - start), "empty input (no JSON value)" });

		Value value = Value::Parse(&cur);
		if (value.IsInvalid())
		{
			// cur 은 파싱이 막힌 지점을 가리킨다. 그 지점 문자로 사유를 추론한다.
			const auto pos = static_cast<std::size_t>(cur - start);
			ne::string_t message = (*cur == '\0')
				                       ? ne::string_t{ "unexpected end of input" }
				                       : std::format("unexpected character '{}' at offset {}", *cur, pos);
			return ne::Result<Value, ParseError>::Error(ParseError{ pos, std::move(message) });
		}

		// 값 뒤에 공백 아닌 후행 데이터
		if (internal::SkipWhitespace(&cur))
		{
			const auto pos = static_cast<std::size_t>(cur - start);
			return ne::Result<Value, ParseError>::Error(ParseError{ pos, std::format("trailing characters after value at offset {}", pos) });
		}

		return ne::Result<Value, ParseError>::Ok(std::move(value));
	}

	Value Parse(lpcstr_t _data)
	{
		auto result = TryParse(_data);
		return result.IsOk() ? std::move(result.Value()) : Value{};
	}
}
