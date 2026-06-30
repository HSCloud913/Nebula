//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include "Coroutine/Task.h"
#include "Http/HttpCommon.h"
#include "Http/Http2/FlowControl.h"
#include "Http/Http2/Hpack.h"
#include "Stream/IStream.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::network::http_2)
	using Http2Handler = std::function<
		ne::Task<ne::network::HttpResponse>(ne::network::HttpRequest)>;

	// HTTP/2 서버 커넥션 핸들러.
	// 단일 IStream 위에서 요청 수신 → 핸들러 호출 → 응답 전송.
	class Http2Server
	{
	public:
		NEBULA_NON_COPYABLE(Http2Server)

		explicit Http2Server(std::unique_ptr<ne::network::IStream> _stream,
		                     Http2Handler _handler) noexcept;
		Http2Server(Http2Server&&) noexcept = default;
		Http2Server& operator=(Http2Server&&) noexcept = default;
		~Http2Server();

	private:
		std::unique_ptr<ne::network::IStream> stream;
		Http2Handler handler;
		FlowController flowCtrl;

	public:
		[[nodiscard]] ne::Task<ne::Result<void, ne::HttpError>> Run();

	private:
		[[nodiscard]] ne::Task<ne::Result<void, ne::HttpError>> SendPreface();
		[[nodiscard]] ne::Task<ne::Result<void, ne::HttpError>> SendSettings();
		[[nodiscard]] ne::Task<ne::Result<void, ne::HttpError>>
		SendHeaders(uint32_t _streamId, const ne::network::HttpResponse& _resp);
		[[nodiscard]] ne::Task<ne::Result<void, ne::HttpError>>
		SendData(uint32_t _streamId, const ne::string_t& _body);
	};
END_NS
