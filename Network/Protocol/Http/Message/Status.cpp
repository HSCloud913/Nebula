//
// Created by hscloud on 26. 7. 20.
//

#include "Network/Protocol/Http/Message/Status.h"



namespace ne::network::http
{
	string_view_t DefaultReasonPhrase(const int_t _statusCode) noexcept
	{
		switch (_statusCode)
		{
			case 200:
				return "OK";
			case 201:
				return "Created";
			case 202:
				return "Accepted";
			case 204:
				return "No Content";
			case 301:
				return "Moved Permanently";
			case 302:
				return "Found";
			case 304:
				return "Not Modified";
			case 400:
				return "Bad Request";
			case 401:
				return "Unauthorized";
			case 403:
				return "Forbidden";
			case 404:
				return "Not Found";
			case 405:
				return "Method Not Allowed";
			case 409:
				return "Conflict";
			case 413:
				return "Payload Too Large";
			case 429:
				return "Too Many Requests";
			case 500:
				return "Internal Server Error";
			case 501:
				return "Not Implemented";
			case 502:
				return "Bad Gateway";
			case 503:
				return "Service Unavailable";
			default:
				return "";
		}
	}
}
