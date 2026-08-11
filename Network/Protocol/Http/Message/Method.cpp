//
// Created by hscloud on 26. 7. 20.
//

#include "Network/Protocol/Http/Message/Method.h"



namespace ne::network::http
{
	string_view_t ToString(const Method _method) noexcept
	{
		switch (_method)
		{
			case Method::GET:
				return "GET";
			case Method::POST:
				return "POST";
			case Method::PUT:
				return "PUT";
			case Method::DELETE_:
				return "DELETE";
			case Method::HEAD:
				return "HEAD";
			case Method::OPTIONS:
				return "OPTIONS";
			case Method::PATCH:
				return "PATCH";
			case Method::CONNECT:
				return "CONNECT";
			case Method::TRACE:
				return "TRACE";
			case Method::UNKNOWN:
				break;
		}

		return "";
	}

	Method MethodFromString(const string_view_t _text) noexcept
	{
		if (_text == "GET") return Method::GET;
		if (_text == "POST") return Method::POST;
		if (_text == "PUT") return Method::PUT;
		if (_text == "DELETE") return Method::DELETE_;
		if (_text == "HEAD") return Method::HEAD;
		if (_text == "OPTIONS") return Method::OPTIONS;
		if (_text == "PATCH") return Method::PATCH;
		if (_text == "CONNECT") return Method::CONNECT;
		if (_text == "TRACE") return Method::TRACE;

		return Method::UNKNOWN;
	}
}
