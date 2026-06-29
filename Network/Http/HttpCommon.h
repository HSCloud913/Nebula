//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstdint>
#include <vector>
#include "Type.h"

BEGIN_NS(ne::network)
	enum class HttpMethod : uint8_t
	{
		GET,
		POST,
		PUT,
		DEL,
		PATCH,
		HEAD,
		OPTIONS
	};

	namespace HttpStatusCode
	{
		inline constexpr uint16_t Ok = 200;
		inline constexpr uint16_t Created = 201;
		inline constexpr uint16_t NoContent = 204;
		inline constexpr uint16_t BadRequest = 400;
		inline constexpr uint16_t Unauthorized = 401;
		inline constexpr uint16_t Forbidden = 403;
		inline constexpr uint16_t NotFound = 404;
		inline constexpr uint16_t InternalServerError = 500;
	}

	using HttpHeaders = std::vector<std::pair<string_t, string_t>>;

	struct HttpRequest
	{
		HttpMethod method{ HttpMethod::GET };
		string_t path;
		HttpHeaders headers;
		string_t body;
	};

	struct HttpResponse
	{
		uint16_t status{};
		string_t statusText;
		HttpHeaders headers;
		string_t body;
	};

END_NS
