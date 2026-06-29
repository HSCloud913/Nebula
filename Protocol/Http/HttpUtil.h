//
// Created by nebula on 24. 5. 29.
//

#ifndef HTTPUTIL_H
#define HTTPUTIL_H

#include <charconv>
#include <concepts>
#include <ranges>
#include <vector>
#include <optional>
#include "HttpBase.h"
#include "StringFormat.h"

BEGIN_NS(ne::protocol::Http)
	/* Template */
	template <typename T, typename ... U>
	concept IsAnyOf = (std::same_as<T, U> || ...);

	template <typename T>
	concept IsByte = IsAnyOf<std::remove_cvref_t<T>, std::byte, char_t, byte_t>;

	template <typename T>
	concept IsByteData = IsByte<T> || std::ranges::range<T> && IsByte<std::ranges::range_value_t<T>>;

	template <typename Range_, typename Value_>
	concept IsInputRangeOf = std::ranges::input_range<Range_> && std::same_as<std::ranges::range_value_t<Range_>, Value_>;

	template <typename Range_, typename Value_>
	concept IsSizedRangeOf = IsInputRangeOf<Range_, Value_> && std::ranges::sized_range<Range_>;


	/* Wrapper function */
	constexpr auto Filter = std::views::filter([](const auto& _x)
	{
		return static_cast<bool_t>(_x);
	});
	constexpr auto DereferenceMove = std::views::transform([](auto&& _x)
	{
		return std::move(*_x);
	});


	/* Template function */
	template <IsSizedRangeOf<char_t> Range_>
	[[nodiscard]]
	inline string_t RangeToString(const Range_& _range)
	{
		auto result = string_t(_range.size(), char_t{});
		std::ranges::copy(_range, std::ranges::begin(result));
		return result;
	}

	template <IsInputRangeOf<char_t> Range_>
	[[nodiscard]]
	inline string_t RangeToString(const Range_& _range)
	{
		auto result = string_t();
		std::ranges::copy(_range, std::back_inserter(result));
		return result;
	}

	template <typename T>
	concept IsHeader = IsAnyOf<T, HeaderCopy, Header>;

	[[nodiscard]]
	inline bool_t IsValidHeaderField(const string_view_t _value) noexcept
	{
		return _value.find_first_of("\r\n") == string_view_t::npos;
	}
	constexpr auto RangeToStringView = []<IsInputRangeOf<char_t> Range_>(Range_&& _range)
	{
		return string_view_t(&*std::ranges::begin(_range), static_cast<string_view_t::size_type>(std::ranges::distance(_range)));
	};

	template <IsByte Byte_>
	[[nodiscard]]
	inline string_view_t DataToString(const std::span<Byte_> _data)
	{
		return string_view_t(reinterpret_cast<lpcstr_t>(_data.data()), _data.size());
	}

	template <IsByte Byte_>
	[[nodiscard]]
	inline std::span<Byte_ const> StringToData(const string_view_t _string)
	{
		return std::span(reinterpret_cast<const Byte_*>(_string.data()), _string.size());
	}

	template <IsByteData T>
	[[nodiscard]]
	std::size_t SizeOfByteData(const T& _data)
	{
		if constexpr (std::ranges::range<T>)
		{
			return std::ranges::distance(_data);
		}
		else
		{
			return sizeof(_data);
		}
	}

	template <IsByteData Data_, std::ranges::contiguous_range Range_, IsByte RangeValue_ = std::ranges::range_value_t<Range_>>
	[[nodiscard]]
	auto CopyByteData(const Data_& _data, Range_& _range) -> std::ranges::iterator_t<Range_>
	{
		if constexpr (IsByte<Data_>)
		{
			*std::ranges::begin(_range) = *reinterpret_cast<const RangeValue_*>(&_data);
			return std::ranges::begin(_range) + 1;
		}
		else
		{
			return std::ranges::copy(std::span(reinterpret_cast<RangeValue_ const*>(std::ranges::data(_data)), std::ranges::size(_data)), std::ranges::begin(_range)).out;
		}
	}

	template <IsByteData ... T>
	[[nodiscard]]
	std::vector<std::byte> ConcatenateByteData(const T& ... _arguments)
	{
		auto buffer = std::vector<std::byte>((SizeOfByteData(_arguments) + ...));
		auto bufferSpan = std::span(buffer);
		((bufferSpan = std::span(CopyByteData(_arguments, bufferSpan), bufferSpan.end())), ...);
		return buffer;
	}

	template <std::integral T>
	[[nodiscard]]
	std::optional<T> StringToInt(const string_view_t _string, const int_t _base = 10)
	{
		auto result = T{};
		if (std::from_chars(_string.data(), _string.data() + _string.size(), result, _base).ec == std::errc{})
		{
			return result;
		}
		return {};
	}

	template <std::ranges::input_range Range_, IsHeader Header_ = std::ranges::range_value_t<Range_>>
	[[nodiscard]]
	inline Header_ const* FindHeaderByName(const Range_& _headers, const string_view_t _name)
	{
		auto const pos = std::ranges::find_if(_headers, [&](const Header_& header)
		{
			return std::ranges::equal(RangeToString(_name | StringFormat::LowerCaseTransform), header.name | StringFormat::LowerCaseTransform);
		});

		return (pos == std::ranges::end(_headers)) ? nullptr : &*pos;
	}


	/* Function */
	[[nodiscard]]
	UrlElement SplitUrl(const string_view_t _url);

END_NS

#endif //HTTPUTIL_H
