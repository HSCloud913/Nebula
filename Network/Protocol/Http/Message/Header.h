//
// Created by hscloud on 26. 7. 24.
//

#pragma once
#include <initializer_list>
#include <map>
#include <optional>
#include <utility>
#include <vector>
#include "Base/Type.h"
#include "Util/StringFormat.h"

namespace ne::network::http
{
	// 헤더 이름 대소문자 무시 비교자(RFC 9110 §5.1 — 필드 이름은 case-insensitive).
	struct HeaderNameLess
	{
		[[nodiscard]] bool_t operator()(const string_t& _lhs, const string_t& _rhs) const noexcept { return ne::util::StringFormat::CompareIgnoreCase(_lhs, _rhs) < 0; }
	};

	/**
	 * @class Headers
	 * @brief HTTP 헤더 필드 목록입니다.
	 *
	 * 필드 이름은 대소문자를 구분하지 않고 비교하며, 같은 이름이 여러 번 나타날 수 있습니다
	 * (예: Set-Cookie). Get()은 첫 매치만, GetAll()은 같은 이름의 모든 값을 반환합니다.
	 */
	class Headers
	{
	public:
		Headers() = default;

		// {"Authorization","Bearer ..."} 처럼 이니셜라이저 리스트로 바로 만들 수 있게 하는 편의 생성자.
		Headers(std::initializer_list<std::pair<string_view_t, string_view_t>> _entries)
		{
			for (const auto& [name, value] : _entries) Add(name, value);
		}

	private:
		std::multimap<string_t, string_t, HeaderNameLess> entries;

	public:
		void_t Add(string_view_t _name, string_view_t _value) { entries.emplace(string_t(_name), string_t(_value)); }
		void_t Set(string_view_t _name, string_view_t _value)
		{
			entries.erase(string_t(_name));
			Add(_name, _value);
		}

		/** @brief _name 의 모든 항목을 제거합니다(없으면 아무 일도 하지 않음). */
		void_t Remove(string_view_t _name) { entries.erase(string_t(_name)); }

		[[nodiscard]] std::optional<string_view_t> Get(string_view_t _name) const noexcept
		{
			const auto iter = entries.find(string_t(_name));
			return iter == entries.end() ? std::nullopt : std::optional<string_view_t>(iter->second);
		}

		[[nodiscard]] std::vector<string_view_t> GetAll(string_view_t _name) const
		{
			std::vector<string_view_t> result;

			const auto range = entries.equal_range(string_t(_name));
			for (auto iter = range.first; iter != range.second; ++iter) result.emplace_back(iter->second);

			return result;
		}

		[[nodiscard]] bool_t Has(string_view_t _name) const noexcept { return entries.contains(string_t(_name)); }
		void_t Clear() noexcept { entries.clear(); }

	public:
		[[nodiscard]] auto begin() const noexcept { return entries.begin(); }
		[[nodiscard]] auto end() const noexcept { return entries.end(); }
	};

	/**
	 * @brief 필드가 CR/LF 를 포함하지 않는지(인젝션 안전) 검사합니다.
	 *
	 * 헤더 이름/값·요청 target·응답 reason 에 raw CR/LF 가 섞이면 헤더 인젝션(response splitting)이나
	 * 요청 스머글링으로 이어질 수 있으므로, 직렬화 전에 이 검사로 거릅니다(RFC 9110 §5.5).
	 */
	[[nodiscard]] inline bool_t IsInjectionSafe(const string_view_t _text) noexcept { return _text.find_first_of("\r\n") == string_view_t::npos; }

	/** @brief 모든 헤더의 이름과 값이 인젝션 안전한지 검사합니다. */
	[[nodiscard]] inline bool_t HeadersAreInjectionSafe(const Headers& _headers) noexcept
	{
		for (const auto& [name, value] : _headers) if (!IsInjectionSafe(name) || !IsInjectionSafe(value)) return false;

		return true;
	}
}
