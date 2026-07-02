//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include "../HttpCommon.h"
#include "Coroutine/Task.h"
#include "Engine/IIoEngine.h"
#include "Result.h"
#include "Error.h"
#include <functional>
#include <cstddef>
#include "Socket/Socket.h"

BEGIN_NS(ne::network::http_1)
	// HTTP/1.1 서버.
	// 요청마다 순차 처리 (연결 한 개씩 직렬 처리).
	// RequestHandler 는 복사 가능한 callable 이어야 함 (std::function 제약).
	class Server
	{
	public:
		using RequestHandler = std::function<ne::Task<HttpResponse>(const HttpRequest&)>;

	public:
		Server(ne::io::IIoEngine& _engine, RequestHandler _handler) noexcept;
		~Server() = default;

		NEBULA_NON_COPYABLE(Server)
		NEBULA_DEFAULT_MOVE(Server)

	private:
		ne::io::IIoEngine* engine;
		RequestHandler handler;
		bool_t running{ false };

	public:
		// Bind + listen + accept 루프 실행. Stop() 호출 또는 치명적 에러 시 반환.
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> Run(string_view_t _host, uint16_t _port);
		void Stop() noexcept;

	private:
		ne::Task<void> HandleConnection(Socket _sock);

	private:
		static ne::Result<HttpRequest, ne::OsError> ParseRequest(string_view_t _raw);
		static std::size_t ParseContentLength(string_view_t _headers) noexcept;
		static string_t SerializeResponse(const HttpResponse& _response);
		static string_view_t DefaultStatusText(uint16_t _code) noexcept;
		static HttpMethod ParseMethod(string_view_t _method) noexcept;
	};

END_NS
