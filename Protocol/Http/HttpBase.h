//
// Created by nebula on 24. 5. 29.
//

#ifndef HTTPBASE_H
#define HTTPBASE_H

#include "Type.h"

BEGIN_NS(ne::protocol::Http)
	/* enum */
	enum class Protocol
	{
		UNKNOWN = -1,
		HTTP    = 80,
		HTTPS   = 443
	};

	enum class StatusCode
	{
		UNKNOWN                         = -1,	/* Default value */
		CONTINUE                        = 100,	/* Information */
		SWITCHING_PROTOCOLS             = 101,
		PROCESSING                      = 102,
		EARLY_HINTS                     = 103,
		OK                              = 200,	/* Success */
		CREATED                         = 201,
		ACCEPTED                        = 202,
		NON_AUTH_ORITATIVE_INFORMATION  = 203,
		NO_CONTENT                      = 204,
		RESET_CONTENT                   = 205,
		PARTIAL_CONTENT                 = 206,
		MULTI_STATUS                    = 207,
		ALREADY_REPORTED                = 208,
		IM_USED                         = 226,
		MULTIPLE_CHOICES                = 300,	/* Redirect */
		MOVED_PERMANENTLY               = 301,
		FOUND                           = 302,
		SEE_OTHER                       = 303,
		NOT_MODIFIED                    = 304,
		USE_PROXY                       = 305,	// Unused
		SWITCH_PROXY                    = 306,	// Unused
		TEMPORARY_REDIRECT              = 307,
		PERMANENT_REDIRECT              = 308,
		BAD_REQUEST                     = 400,	/* Client error */
		UNAUTHORIZED                    = 401,
		PAYMENT_REQUIRED                = 402,	// Unused
		FORBIDDEN                       = 403,
		NOT_FOUND                       = 404,
		METHOD_NOT_ALLOWED              = 405,
		NOT_ACCEPTABLE                  = 406,
		PROXY_AUTHENTICATION_REQUIRED   = 407,
		REQUEST_TIMEOUT                 = 408,
		CONFLICT                        = 409,
		GONE                            = 410,
		LENGTH_REQUIRED                 = 411,
		PRECONDITION_FAILED             = 412,
		PAYLOAD_TOO_LARGE               = 413,
		URI_TOO_LONG                    = 414,
		UNSUPPORTED_MEDIA_TYPE          = 415,
		RANGE_NOT_SATISFIABLE           = 416,
		EXPECTATION_FAILED              = 417,
		IM_A_TEAPOT                     = 418,	// Unused
		MISDIRECTED_REQUEST             = 421,
		UNPROCESSABLE_ENTITY            = 422,
		LOCKED                          = 423,
		FAILED_DEPENDENCY               = 424,
		TOO_EARLY                       = 425,
		UPGRADE_REQUIRED                = 426,
		PRECONDITION_REQUIRED           = 428,
		TOO_MANY_REQUESTS               = 429,
		REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
		UNAVAILABLE_FOR_LEGAL_REASONS   = 451,
		INTERNAL_SERVER_ERROR           = 500,	/* Server error */
		NOT_IMPLEMENTED                 = 501,
		BAD_GATEWAY                     = 502,
		SERVICE_UNAVAILABLE             = 503,
		GATEWAY_TIMEOUT                 = 504,
		HTTP_VERSION_NOT_SUPPORTED      = 505,
		VARIANT_ALSO_NEGOTIATES         = 506,
		INSUFFICIENT_STORAGE            = 507,
		LOOP_DETECTED                   = 508,
		NOT_EXTENDED                    = 510,
		NETWORK_AUTHENTICATION_REQUIRED = 511
	};

	enum class Method
	{
		CONNECT,
		DEL,
		GET,
		HEAD,
		OPTIONS,
		PATCH,
		POST,
		PUT,
		TRACE
	};

	enum class Mode
	{
		Chunked,
		Compress,
		Deflate,
		GZip,
		Identify
	};

	/* Struct */
	struct Status
	{
		string_t httpVersion;
		StatusCode statusCode = StatusCode::UNKNOWN;
		string_t statusMessage;

		[[nodiscard]]
		bool_t operator==(const Status&) const noexcept = default;
	};

	struct HostAndPort
	{
		string_view_t host;
		std::optional<int_t> port;
	};

	struct UrlElement
	{
		Protocol protocol = Protocol::UNKNOWN;
		string_view_t host;
		int_t port = 0;
		string_view_t path;
	};

	struct Header;

	struct HeaderCopy
	{
		string_t name, value;

		[[nodiscard]]
		inline explicit operator Header() const;
	};

	struct Header
	{
		string_view_t name, value;

		[[nodiscard]]
		explicit operator HeaderCopy() const
		{
			return HeaderCopy{ .name = string_t{ name }, .value = string_t{ value } };
		}
	};

	HeaderCopy::operator Header() const
	{
		return Header{ .name = string_view_t{ name }, .value = string_view_t{ value } };
	}

	struct ResponseData
	{
		NEBULA_NON_COPYABLE(ResponseData)
		NEBULA_DEFAULT_MOVE(ResponseData)

		Method method;
		string_t path;
		string_t uri;
		Status status;
		string_t headersString;
		std::vector<Header> headers;
		std::vector<std::byte> body;

		[[nodiscard]]
		bool_t operator==(ResponseData const&) const noexcept = default;

		ResponseData() = default;
		ResponseData(Method _method, string_t _path, Status _status, string_t _headersString = {}, std::vector<Header> _headers = {}, std::vector<std::byte> _body = {})
			: method(_method)
			, path(std::move(_path))
			, status(std::move(_status))
			, headersString(std::move(_headersString))
			, headers(std::move(_headers))
			, body(std::move(_body))
		{
		}
	};

END_NS

#endif //HTTPBASE_H
