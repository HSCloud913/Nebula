//
// Created by hscloud on 26. 7. 29.
//

#pragma once
#include <optional>
#include <utility>
#include <vector>
#include "Base/Type.h"

namespace ne::network::http
{
	/**
	 * @brief 퍼센트 인코딩(%XX)을 디코딩합니다. _plusAsSpace 면 '+' 를 공백으로도 변환합니다(쿼리 이름/값용).
	 * @note 잘못된 %XX 시퀀스는 디코딩하지 않고 그대로 둡니다(관대한 처리).
	 */
	[[nodiscard]] string_t UrlDecode(string_view_t _text, bool_t _plusAsSpace = false);

	/**
	 * @class PathParams
	 * @brief 라우트 패턴("{name}", "{*name}")이 요청 경로에서 추출한 파라미터 목록입니다.
	 *
	 * 서버 라우터(Router)가 매칭 시 채워 핸들러에 넘깁니다. "{name}" 캡처는 퍼센트 디코딩되어
	 * 저장되고, "{*name}"(catch-all) 캡처는 슬래시를 포함할 수 있어 원문 그대로 저장됩니다.
	 */
	class PathParams
	{
	public:
		PathParams() = default;

	private:
		std::vector<std::pair<string_t, string_t>> params;

	public:
		void_t Add(const string_view_t _name, string_t _value) { params.emplace_back(string_t(_name), std::move(_value)); }

		[[nodiscard]] std::optional<string_view_t> Get(const string_view_t _name) const noexcept
		{
			for (const auto& [name, value] : params)
			{
				if (name == _name) return value;
			}
			return std::nullopt;
		}

		[[nodiscard]] bool_t Has(const string_view_t _name) const noexcept { return Get(_name).has_value(); }
		[[nodiscard]] bool_t IsEmpty() const noexcept { return params.empty(); }
		[[nodiscard]] std::size_t Count() const noexcept { return params.size(); }
	};

	/**
	 * @class QueryParams
	 * @brief 요청 target 의 쿼리스트링("?a=1&b=2")을 파싱한 파라미터 목록입니다.
	 *
	 * 핸들러가 필요할 때 Parse(request.target) 로 만듭니다. 이름/값 모두 퍼센트 디코딩되며
	 * '+' 는 공백으로 변환됩니다. 같은 이름이 여러 번 나타날 수 있습니다("?tag=a&tag=b") —
	 * Get() 은 첫 매치만, GetAll() 은 전부 반환합니다. 값 없는 파라미터("?debug")는 빈 값으로
	 * 저장되어 Has() 로 존재 여부를 확인할 수 있습니다.
	 */
	class QueryParams
	{
	public:
		QueryParams() = default;

		/** @brief 요청 target("/path?a=1&b=2")의 '?' 이후를 파싱합니다. '?' 가 없으면 빈 목록. */
		[[nodiscard]] static QueryParams Parse(string_view_t _target);

	private:
		std::vector<std::pair<string_t, string_t>> params;

	public:
		[[nodiscard]] std::optional<string_view_t> Get(const string_view_t _name) const noexcept
		{
			for (const auto& [name, value] : params)
			{
				if (name == _name) return value;
			}
			return std::nullopt;
		}

		[[nodiscard]] std::vector<string_view_t> GetAll(const string_view_t _name) const
		{
			std::vector<string_view_t> result;
			for (const auto& [name, value] : params)
			{
				if (name == _name) result.push_back(value);
			}
			return result;
		}

		[[nodiscard]] bool_t Has(const string_view_t _name) const noexcept
		{
			for (const auto& [name, value] : params)
			{
				if (name == _name) return true;
			}
			return false;
		}

		[[nodiscard]] bool_t IsEmpty() const noexcept { return params.empty(); }
		[[nodiscard]] std::size_t Count() const noexcept { return params.size(); }
	};
}
