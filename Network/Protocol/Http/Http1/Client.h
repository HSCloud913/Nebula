//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include "../HttpCommon.h"
#include "Coroutine/Task.h"
#include "Engine/IIoEngine.h"
#include "Result.h"
#include "Error.h"

BEGIN_NS(ne::network::http_1)
	// HTTP/1.1 클라이언트.
	// ne::io::IIoEngine 기반 코루틴 비동기 I/O 사용.
	// Connection: close 전략 — 요청마다 새 TCP 연결 생성.
	class Client
	{
	public:
		explicit Client(ne::io::IIoEngine& _engine) noexcept;
		~Client() = default;

		NEBULA_NON_COPYABLE(Client)
		NEBULA_DEFAULT_MOVE(Client)

	private:
		ne::io::IIoEngine& engine;

	public:
		[[nodiscard]] ne::Task<ne::Result<HttpResponse, ne::OsError>> Get(string_view_t _host, uint16_t _port, string_view_t _path, HttpHeaders _headers = {}) const;
		[[nodiscard]] ne::Task<ne::Result<HttpResponse, ne::OsError>> Post(string_view_t _host, uint16_t _port, string_view_t _path, string_view_t _body, HttpHeaders _headers = {}) const;

	private:
		ne::Task<ne::Result<HttpResponse, ne::OsError>> Execute(string_view_t _host, uint16_t _port, HttpRequest _request) const;

	private:
		static string_t Serialize(const HttpRequest& _request, string_view_t _host);
		static ne::Result<HttpResponse, ne::OsError> ParseResponse(string_view_t _raw);
	};

END_NS
