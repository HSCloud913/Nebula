//
// Created by nebula on 24. 6. 17.
//

#include "Response.h"

#include <sstream>
#include <chrono>

#include "Exception.h"



BEGIN_NS(ne::protocol::Http::Server)
	[[nodiscard]]
	inline const char_t* GetStatusMessage(const StatusCode _statusCode)
	{
		using enum StatusCode;
		switch (_statusCode)
		{
		case CONTINUE: return "Continue";
		case SWITCHING_PROTOCOLS: return "Switching Protocol";
		case PROCESSING: return "Processing";
		case EARLY_HINTS: return "Early Hints";
		case OK: return "OK";
		case CREATED: return "Created";
		case ACCEPTED: return "Accepted";
		case NON_AUTH_ORITATIVE_INFORMATION: return "Non-Authoritative Information";
		case NO_CONTENT: return "No Content";
		case RESET_CONTENT: return "Reset Content";
		case PARTIAL_CONTENT: return "Partial Content";
		case MULTI_STATUS: return "Multi-Status";
		case ALREADY_REPORTED: return "Already Reported";
		case IM_USED: return "IM Used";
		case MULTIPLE_CHOICES: return "Multiple Choices";
		case MOVED_PERMANENTLY: return "Moved Permanently";
		case FOUND: return "Found";
		case SEE_OTHER: return "See Other";
		case NOT_MODIFIED: return "Not Modified";
		case USE_PROXY: return "Use Proxy";
		case SWITCH_PROXY: return "unused";
		case TEMPORARY_REDIRECT: return "Temporary Redirect";
		case PERMANENT_REDIRECT: return "Permanent Redirect";
		case BAD_REQUEST: return "Bad Request";
		case UNAUTHORIZED: return "Unauthorized";
		case PAYMENT_REQUIRED: return "Payment Required";
		case FORBIDDEN: return "Forbidden";
		case NOT_FOUND: return "Not Found";
		case METHOD_NOT_ALLOWED: return "Method Not Allowed";
		case NOT_ACCEPTABLE: return "Not Acceptable";
		case PROXY_AUTHENTICATION_REQUIRED: return "Proxy Authentication Required";
		case REQUEST_TIMEOUT: return "Request Timeout";
		case CONFLICT: return "Conflict";
		case GONE: return "Gone";
		case LENGTH_REQUIRED: return "Length Required";
		case PRECONDITION_FAILED: return "Precondition Failed";
		case PAYLOAD_TOO_LARGE: return "Payload Too Large";
		case URI_TOO_LONG: return "URI Too Long";
		case UNSUPPORTED_MEDIA_TYPE: return "Unsupported Media Type";
		case RANGE_NOT_SATISFIABLE: return "Range Not Satisfiable";
		case EXPECTATION_FAILED: return "Expectation Failed";
		case IM_A_TEAPOT: return "I'm a teapot";
		case MISDIRECTED_REQUEST: return "Misdirected Request";
		case UNPROCESSABLE_ENTITY: return "Unprocessable Content";
		case LOCKED: return "Locked";
		case FAILED_DEPENDENCY: return "Failed Dependency";
		case TOO_EARLY: return "Too Early";
		case UPGRADE_REQUIRED: return "Upgrade Required";
		case PRECONDITION_REQUIRED: return "Precondition Required";
		case TOO_MANY_REQUESTS: return "Too Many Requests";
		case REQUEST_HEADER_FIELDS_TOO_LARGE: return "Request Header Fields Too Large";
		case UNAVAILABLE_FOR_LEGAL_REASONS: return "Unavailable For Legal Reasons";
		case INTERNAL_SERVER_ERROR: return "Internal Server Error";
		case NOT_IMPLEMENTED: return "Not Implemented";
		case BAD_GATEWAY: return "Bad Gateway";
		case SERVICE_UNAVAILABLE: return "Service Unavailable";
		case GATEWAY_TIMEOUT: return "Gateway Timeout";
		case HTTP_VERSION_NOT_SUPPORTED: return "HTTP Version Not Supported";
		case VARIANT_ALSO_NEGOTIATES: return "Variant Also Negotiates";
		case INSUFFICIENT_STORAGE: return "Insufficient Storage";
		case LOOP_DETECTED: return "Loop Detected";
		case NOT_EXTENDED: return "Not Extended";
		case NETWORK_AUTHENTICATION_REQUIRED: return "Network Authentication Required";
		default: return "Internal Server Error";
		}
	}



	/*--------------------------------------------------*/



	Response::Response()
	{
		status.httpVersion = "HTTP/1.1";

		AddHeaders(
			{
				{ .name = "Access-Control-Allow-Origin", .value = "*" },
				{ .name = "Content-Type", .value = "application/json" },
				{ .name = "X-Content-Type", .value = "nosniff" },
				{ .name = "Server", .value = "Nebula" }
			}
		);
	}



	void_t Response::SetStatusCode(const StatusCode _statusCode)
	{
		status.statusCode = _statusCode;
		status.statusMessage = GetStatusMessage(status.statusCode);
	}


	void_t Response::AddHeader(const Header& _header)
	{
		if (!IsValidHeaderField(_header.name) || !IsValidHeaderField(_header.value))
		{
			throw ne::Exception("[Response/AddHeader]", "Header name or value must not contain CR/LF characters");
		}

		AddHeaders(std::format("{}: {}", _header.name, _header.value));
	}

	void_t Response::AddHeaders(const string_view_t _headersString)
	{
		if (_headersString.empty()) return;

		headers += _headersString;
		if (_headersString.back() != '\n') headers += "\r\n";
	}

	void_t Response::AddHeaders(const std::initializer_list<const Header> _headers)
	{
		AddHeaders(std::span(_headers));
	}


	void_t Response::SetBody(const string_view_t _body)
	{
		SetBody(StringToData<std::byte>(_body));
	}



	std::vector<std::byte> Response::GetResponseString(const Mode _mode)
	{
		using namespace std::string_view_literals;

		auto now = std::chrono::system_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(now);

		std::stringstream ss;
		ss << std::put_time(std::gmtime(&time_t), "%a, %d %b %Y %H:%M:%S") << " GMT";

		AddHeader({ .name = "Date", .value = ss.str() });

		if (!body.empty())
		{
			if (_mode == Mode::Identify)
			{
				AddHeader({ .name = "Content-Length", .value = std::to_string(body.size()) });
			}
			else if (_mode == Mode::Chunked)
			{
				AddHeader({ .name = "Transfer-Encoding", .value = "chunked" });
			}
		}

		auto statusCode = std::to_string(static_cast<int_t>(status.statusCode));
		if (_mode == Mode::Identify)
		{
			return ConcatenateByteData(
				status.httpVersion,
				' ',
				statusCode,
				' ',
				status.statusMessage,
				"\r\n"sv,
				headers,
				"\r\n"sv,
				body
			);
		}
		if (_mode == Mode::Chunked)
		{
			std::stringstream ss;
			ss << std::hex << body.size();

			return ConcatenateByteData(
				status.httpVersion,
				' ',
				statusCode,
				' ',
				status.statusMessage,
				"\r\n"sv,
				headers,
				"\r\n"sv,
				ss.str(),
				"\r\n"sv,
				body,
				"\r\n0\r\n\r\n"sv
			);
		}

		throw ne::Exception("[Response/GetResponseString]", "Invalid transport mode");
	}


END_NS
