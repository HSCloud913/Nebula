//
// Created by nebula on 24. 5. 17.
//

#pragma once
#include <concepts>
#include <cstring>
#include <vector>
#include <memory>
#include "Util/Ascii.h"

BEGIN_NS(ne)
	enum class TokenizeOption
	{
		NONE,
		IGNORE_EMPTY,
		TRIM
	};

	/**
	* @brief StringFormat 템플릿 함수들이 요구하는 최소 인터페이스: std::basic_string 특수화만 허용한다.
	*
	* (std::string_view 등은 resize/erase 가 없어 *InPlace 계열과 호환되지 않으므로 의도적으로 제외)
	*/
	template <typename T>
	concept StringLike = requires
	{
		typename T::value_type;
		typename T::traits_type;
		typename T::allocator_type;
	} && std::same_as<T, std::basic_string<typename T::value_type, typename T::traits_type, typename T::allocator_type>>;

	/**
	 * @class StringFormat
	 * @brief 문자열 트림/대소문자 변환/치환/비교/토큰화 등을 제공하는 정적 유틸리티 클래스입니다.
	 *
	 * `std::string`/`std::wstring` 등 문자열류 타입 T에 대해 템플릿으로 동작하며,
	 * Windows에서는 MBCS/WCS/UTF-8 상호 변환 함수도 함께 제공합니다.
	 */
	class NEBULA_API StringFormat final
	{
	private:
		explicit StringFormat() = default;
		~StringFormat() = default;

	public:
		template <StringLike T>
		static T Trim(const T& _source);
		template <StringLike T>
		static T& TrimInPlace(T& _source);

		template <StringLike T>
		static T TrimLeft(const T& _source);
		template <StringLike T>
		static T& TrimLeftInPlace(T& _source);

		template <StringLike T>
		static T TrimRight(const T& _source);
		template <StringLike T>
		static T& TrimRightInPlace(T& _source);

	public:
		template <StringLike T>
		static T Lower(const T& _source);
		template <StringLike T>
		static T& LowerInPlace(T& _source);

		template <StringLike T>
		static T Upper(const T& _source);
		template <StringLike T>
		static T& UpperInPlace(T& _source);

	public:
		template <StringLike T>
		static T Replace(T& _source, const T& _from, const T& _to, T::size_type _start = 0);
		template <StringLike T>
		static T Replace(T& _source, const T::value_type* _from, const T::value_type* _to, T::size_type _start = 0);

		template <StringLike T>
		static T& ReplaceInPlace(T& _source, const T& _from, const T& _to, T::size_type _start = 0);
		template <StringLike T>
		static T& ReplaceInPlace(T& _source, const T::value_type* _from, const T::value_type* _to, T::size_type _start = 0);

	public:
		template <StringLike T>
		static int_t Compare(const T& _lhs, const T& _rhs);
		template <StringLike T>
		static int_t CompareIgnoreCase(const T& _lhs, const T& _rhs);

	public:
		template <StringLike T>
		static bool_t Tokenize(const T& _source, const T& _separators, std::vector<T>& _tokens, TokenizeOption _option = TokenizeOption::NONE);

	public:
		static constexpr auto LowerCaseTransform = std::views::transform([](const char_t _c) { return static_cast<char_t>(std::tolower(static_cast<byte_t>(_c))); });

		static constexpr auto UpperCaseTransform = std::views::transform([](const char_t _c) { return static_cast<char_t>(std::toupper(static_cast<byte_t>(_c))); });

		static constexpr auto LowerCaseWideTransform = std::views::transform([](const wchar_t _c) { return static_cast<wchar_t>(std::towlower(static_cast<ushort_t>(_c))); });

		static constexpr auto UpperCaseWideTransform = std::views::transform([](const wchar_t _c) { return static_cast<wchar_t>(std::towupper(static_cast<ushort_t>(_c))); });

		[[nodiscard]] static constexpr bool_t EqualCaseInsensitive(string_view_t _lhs, string_view_t _rhs) noexcept;

		[[nodiscard]] static constexpr bool_t EqualCaseInsensitive(wstring_view_t _lhs, wstring_view_t _rhs) noexcept;

#if defined(_WIN32)
	public:
		static string_t WCStoMBCS(const wchar_t* _wcs);
		static string_t WCStoUTF8(const wchar_t* _wcs);
		static string_t MBCStoUTF8(const char_t* _mbcs);
		static std::wstring MBCStoWCS(const char_t* _mbcs);
		static string_t UTF8toMBCS(const char_t* _utf8);
		static std::wstring UTF8toWCS(const char_t* _utf8);
#endif
	};

	template <StringLike T>
	T StringFormat::Trim(const T& _source)
	{
		int_t first = 0;
		int_t last = static_cast<int_t>(_source.size()) - 1;

		while (first <= last && Ascii::IsSpace(_source[first])) { ++first; }
		while (last >= first && Ascii::IsSpace(_source[last])) { --last; }

		return T(_source, first, last - first + 1);
	}

	template <StringLike T>
	T& StringFormat::TrimInPlace(T& _source)
	{
		int_t first = 0;
		int_t last = static_cast<int_t>(_source.size()) - 1;

		while (first <= last && Ascii::IsSpace(_source[first])) { ++first; }
		while (last >= first && Ascii::IsSpace(_source[last])) { --last; }

		_source.resize(last + 1);
		_source.erase(0, first);

		return _source;
	}


	template <StringLike T>
	T StringFormat::TrimLeft(const T& _source)
	{
		auto iter = _source.begin();
		auto end = _source.end();

		while (iter != end && Ascii::IsSpace(*iter)) { ++iter; }

		return T(iter, end);
	}

	template <StringLike T>
	T& StringFormat::TrimLeftInPlace(T& _source)
	{
		auto iter = _source.begin();
		auto end = _source.end();

		while (iter != end && Ascii::IsSpace(*iter)) { ++iter; }

		_source.erase(_source.begin(), iter);

		return _source;
	}


	template <StringLike T>
	T StringFormat::TrimRight(const T& _source)
	{
		int_t pos = static_cast<int_t>(_source.size()) - 1;

		while (pos >= 0 && Ascii::IsSpace(_source[pos])) { --pos; }

		return T(_source, 0, pos + 1);
	}

	template <StringLike T>
	T& StringFormat::TrimRightInPlace(T& _source)
	{
		int_t pos = static_cast<int_t>(_source.size()) - 1;

		while (pos >= 0 && Ascii::IsSpace(_source[pos])) { --pos; }

		_source.resize(pos + 1);

		return _source;
	}



	template <StringLike T>
	T StringFormat::Lower(const T& _source)
	{
		auto iter = _source.begin();
		auto end = _source.end();

		T result;
		result.reserve(_source.size());
		while (iter != end) { result += static_cast<T::value_type>(Ascii::Lower(*iter++)); }

		return result;
	}

	template <StringLike T>
	T& StringFormat::LowerInPlace(T& _source)
	{
		auto iter = _source.begin();
		auto end = _source.end();

		while (iter != end)
		{
			*iter = static_cast<T::value_type>(Ascii::Lower(*iter));
			++iter;
		}

		return _source;
	}



	template <StringLike T>
	T StringFormat::Upper(const T& _source)
	{
		auto iter = _source.begin();
		auto end = _source.end();

		T result;
		result.reserve(_source.size());
		while (iter != end) { result += static_cast<T::value_type>(Ascii::Upper(*iter++)); }

		return result;
	}

	template <StringLike T>
	T& StringFormat::UpperInPlace(T& _source)
	{
		auto iter = _source.begin();
		auto end = _source.end();

		while (iter != end)
		{
			*iter = static_cast<T::value_type>(Ascii::Upper(*iter));
			++iter;
		}

		return _source;
	}


	template <StringLike T>
	T StringFormat::Replace(T& _source, const T& _from, const T& _to, typename T::size_type _start)
	{
		T result(_source);
		ReplaceInPlace(result, _from, _to, _start);
		return result;
	}

	template <StringLike T>
	T StringFormat::Replace(T& _source, const typename T::value_type* _from, const typename T::value_type* _to, typename T::size_type _start)
	{
		T result(_source);
		ReplaceInPlace(result, _from, _to, _start);
		return result;
	}


	template <StringLike T>
	T& StringFormat::ReplaceInPlace(T& _source, const T& _from, const T& _to, typename T::size_type _start)
	{
		T result;
		result.append(_source, 0, _start);

		typename T::size_type pos = 0;
		do
		{
			pos = _source.find(_from, _start);
			if (pos != T::npos)
			{
				result.append(_source, _start, pos - _start);
				result.append(_to);
				_start = pos + _from.length();
			}
			else { result.append(_source, _start, _source.size() - _start); }
		} while (pos != T::npos);

		_source.swap(result);

		return _source;
	}

	template <StringLike T>
	T& StringFormat::ReplaceInPlace(T& _source, const typename T::value_type* _from, const typename T::value_type* _to, typename T::size_type _start)
	{
		T result;
		result.append(_source, 0, _start);

		typename T::size_type pos = 0;
		typename T::size_type length = std::char_traits<typename T::value_type>::length(_from);

		do
		{
			pos = _source.find(_from, _start);
			if (pos != T::npos)
			{
				result.append(_source, _start, pos - _start);
				result.append(_to);
				_start = pos + length;
			}
			else { result.append(_source, _start, _source.size() - _start); }
		} while (pos != T::npos);

		_source.swap(result);

		return _source;
	}


	template <StringLike T>
	int_t StringFormat::Compare(const T& _lhs, const T& _rhs) { return _lhs.compare(_rhs); }

	template <StringLike T>
	int_t StringFormat::CompareIgnoreCase(const T& _lhs, const T& _rhs) { return Upper(_lhs).compare(Upper(_rhs)); }



	template <StringLike T>
	bool_t StringFormat::Tokenize(const T& _source, const T& _separators, std::vector<T>& _tokens, TokenizeOption _option)
	{
		_tokens.clear();

		auto begin = _source.begin();
		auto end = _source.end();
		while (begin != end)
		{
			if (static_cast<int>(_option) & static_cast<int>(TokenizeOption::TRIM)) { while (begin != end && Ascii::IsSpace(*begin)) { ++begin; } }

			auto iter1 = begin;
			while (iter1 != end && _separators.find(*iter1) == T::npos) { ++iter1; }

			auto iter2 = iter1;
			if (iter2 != begin && (static_cast<int>(_option) & static_cast<int>(TokenizeOption::TRIM)))
			{
				--iter2;
				while (iter2 != begin && Ascii::IsSpace(*iter2)) { --iter2; }

				if (!Ascii::IsSpace(*iter2)) { ++iter2; }
			}

			if (static_cast<int>(_option) & static_cast<int>(TokenizeOption::IGNORE_EMPTY)) { if (iter2 != begin) { _tokens.push_back(T(begin, iter2)); } }
			else { _tokens.push_back(T(begin, iter2)); }

			begin = iter1;
			if (begin != end) { ++begin; }
		}

		return _tokens.empty() ? false : true;
	}



	constexpr bool_t StringFormat::EqualCaseInsensitive(string_view_t _lhs, string_view_t _rhs) noexcept { return std::ranges::equal(_lhs | LowerCaseTransform, _rhs | LowerCaseTransform); }

	constexpr bool_t StringFormat::EqualCaseInsensitive(wstring_view_t _lhs, wstring_view_t _rhs) noexcept { return std::ranges::equal(_lhs | LowerCaseWideTransform, _rhs | LowerCaseWideTransform); }



#if defined(_WIN32)
	string_t StringFormat::WCStoMBCS(const wchar_t* _wcs)
	{
		if (auto required = WideCharToMultiByte(CP_ACP, 0, _wcs, -1, nullptr, 0, nullptr, nullptr); required)
		{
			std::shared_ptr<char_t> buffer(new char_t[required], [](const char_t* _p) { delete[] _p; });
			if (buffer.get())
			{
				memset(buffer.get(), 0, required);
				if (WideCharToMultiByte(CP_ACP, 0, _wcs, -1, buffer.get(), required, nullptr, nullptr)) { return buffer.get(); }
			}
		}

		return "";
	}
	string_t StringFormat::WCStoUTF8(const wchar_t* _wcs)
	{
		if (auto required = WideCharToMultiByte(CP_UTF8, 0, _wcs, -1, nullptr, 0, nullptr, nullptr); required)
		{
			required++;

			std::shared_ptr<char_t> buffer(new char_t[required], [](const char_t* _p) { delete[] _p; });
			if (buffer.get())
			{
				memset(buffer.get(), 0, required);
				required = WideCharToMultiByte(CP_UTF8, 0, _wcs, -1, buffer.get(), required, nullptr, nullptr);
				if (required) { return buffer.get(); }
			}
		}

		return "";
	}
	string_t StringFormat::MBCStoUTF8(const char_t* _mbcs)
	{
		std::wstring wcs = MBCStoWCS(_mbcs);
		if (!wcs.empty()) { return WCStoUTF8(wcs.c_str()); }

		return "";
	}
	std::wstring StringFormat::MBCStoWCS(const char_t* _mbcs)
	{
		if (auto required = MultiByteToWideChar(CP_ACP, 0, _mbcs, -1, nullptr, 0); required)
		{
			std::shared_ptr<wchar_t> buffer(new wchar_t[required], [](const wchar_t* _p) { delete[] _p; });
			if (buffer.get())
			{
				memset(buffer.get(), 0, required * sizeof(wchar_t));
				if (MultiByteToWideChar(CP_ACP, 0, _mbcs, -1, buffer.get(), required)) { return buffer.get(); }
			}
		}

		return L"";
	}
	string_t StringFormat::UTF8toMBCS(const char_t* _utf8)
	{
		std::wstring wcs = UTF8toWCS(_utf8);
		if (!wcs.empty()) { return WCStoMBCS(wcs.c_str()); }

		return "";
	}
	std::wstring StringFormat::UTF8toWCS(const char_t* _utf8)
	{
		if (auto required = MultiByteToWideChar(CP_UTF8, 0, _utf8, -1, nullptr, 0); required)
		{
			required++;

			std::shared_ptr<wchar_t> buffer(new wchar_t[required], [](const wchar_t* _p) { delete[] _p; });
			if (buffer.get())
			{
				memset(buffer.get(), 0, required * sizeof(wchar_t));
				required = MultiByteToWideChar(CP_UTF8, 0, _utf8, -1, buffer.get(), required);
				if (required) { return buffer.get(); }
			}
		}

		return L"";
	}
#endif

END_NS
