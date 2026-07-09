//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstdint>
#include <atomic>
#include <memory>
#include <unordered_map>
#include "Base/Coroutine/Task.h"
#include "Network/Protocol/Http/HttpCommon.h"
#include "Network/Protocol/Http/Http2/FlowControl.h"
#include "Network/Protocol/Http/Http2/Hpack.h"
#include "Network/Stream/IStream.h"
#include "Base/Result.h"
#include "Base/Error.h"
#include "Base/Type.h"

BEGIN_NS(ne::network::http_2)
	// HTTP/2 클라이언트.
	// IStream (TlsStream + ALPN "h2") 위에서 동작.
	class Http2Client
	{
	private:
		explicit Http2Client(std::unique_ptr<ne::network::IStream> _stream) noexcept;

	public:
		Http2Client(Http2Client&&) noexcept = default;
		Http2Client& operator=(Http2Client&&) noexcept = default;
		~Http2Client();

		NEBULA_NON_COPYABLE(Http2Client)

	private:
		std::unique_ptr<ne::network::IStream> stream;
		FlowController flowCtrl;
		std::atomic<uint32_t> nextStreamId{ 1 };

	public:
		[[nodiscard]] static ne::Task<ne::Result<Http2Client, ne::HttpError>>
		Connect(std::unique_ptr<ne::network::IStream> _stream);

		[[nodiscard]] ne::Task<ne::Result<ne::network::HttpResponse, ne::HttpError>>
		Request(const ne::network::HttpRequest& _req);

	private:
		[[nodiscard]] ne::Task<ne::Result<void, ne::HttpError>> SendPreface();
		[[nodiscard]] ne::Task<ne::Result<void, ne::HttpError>> SendSettings();
		[[nodiscard]] ne::Task<ne::Result<void, ne::HttpError>> SendWindowUpdate(uint32_t _streamId, int32_t _increment);
		[[nodiscard]] ne::Task<ne::Result<ne::network::HttpHeaders, ne::HttpError>> ReadHeaders();
	};
END_NS
