//
// Created by hscloud on 26. 7. 29.
//

#include "Network/Protocol/Http/Internal/Router.h"

#include <utility>



namespace ne::network::http::internal
{
	namespace
	{
		// 선행 '/' 를 제거하고 '/' 로 분해한다. "" 또는 "/" → 빈 목록.
		std::vector<string_view_t> SplitSegments(string_view_t _path)
		{
			std::vector<string_view_t> segments;
			if (!_path.empty() && _path.front() == '/') _path.remove_prefix(1);
			if (_path.empty()) return segments;

			while (true)
			{
				const auto slash = _path.find('/');
				if (slash == string_view_t::npos)
				{
					segments.push_back(_path);
					return segments;
				}

				segments.push_back(_path.substr(0, slash));
				_path.remove_prefix(slash + 1);
			}
		}

		[[nodiscard]] bool_t IsParam(const string_t& _segment) noexcept { return _segment.size() >= 2 && _segment.front() == '{' && _segment.back() == '}'; }
	}



	void_t Router::Add(const Method _method, const string_view_t _pattern, RouteHandler _handler)
	{
		Entry entry;
		entry.method = _method;
		for (const auto segment : SplitSegments(_pattern)) entry.segments.emplace_back(segment);
		entry.handler = std::move(_handler);

		routes.push_back(std::move(entry));
	}

	bool_t Router::Match(const std::vector<string_t>& _pattern, const string_view_t _path, PathParams& _params)
	{
		const auto pathSegments = SplitSegments(_path);

		for (std::size_t i = 0; i < _pattern.size(); ++i)
		{
			const string_t& patternSegment = _pattern[i];

			if (IsParam(patternSegment))
			{
				const string_view_t name{ patternSegment.data() + 1, patternSegment.size() - 2 };

				// "{*name}": 남은 경로 전체(빈 값 허용)를 캡처하고 즉시 매치 성공.
				if (!name.empty() && name.front() == '*')
				{
					string_t rest;
					for (std::size_t j = i; j < pathSegments.size(); ++j)
					{
						if (j > i) rest += '/';
						rest.append(pathSegments[j]);
					}

					_params.Add(name.substr(1), std::move(rest));
					return true;
				}

				// "{name}": 비어 있지 않은 세그먼트 하나를 캡처(퍼센트 디코딩).
				if (i >= pathSegments.size() || pathSegments[i].empty()) return false;
				_params.Add(name, UrlDecode(pathSegments[i]));
				continue;
			}

			if (i >= pathSegments.size() || pathSegments[i] != patternSegment) return false;
		}

		return _pattern.size() == pathSegments.size();
	}

	ne::Task<HttpResult<Response>> Router::Dispatch(const Request& _request) const
	{
		using R = HttpResult<Response>;

		// 매칭은 경로만 본다 — 쿼리스트링('?' 이후)은 제외.
		string_view_t path = _request.target;
		if (const auto query = path.find('?'); query != string_view_t::npos) path = path.substr(0, query);

		bool_t isPathMatched = false;
		string_t allow;

		for (const auto& entry : routes)
		{
			PathParams params;
			if (!Match(entry.segments, path, params)) continue;

			isPathMatched = true;
			if (entry.method == _request.method) co_return co_await entry.handler(_request, params);

			// 경로는 맞지만 메서드가 다른 경우 — 405 응답의 Allow 헤더용으로 모아 둔다.
			if (!allow.empty()) allow += ", ";
			allow += string_t(ToString(entry.method));
		}

		if (isPathMatched)
		{
			Response response = Response::Status(405);
			response.headers.Set("Allow", allow);
			co_return R::Ok(std::move(response));
		}

		if (notFound) co_return co_await notFound(_request);

		co_return R::Ok(Response::Status(404));
	}
}
