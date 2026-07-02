//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "Coroutine/Task.h"
#include "../HttpCommon.h"
#include "Frame.h"
#include "Stream/IStream.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::network::http_2)
	enum class StreamState : uint8_t
	{
		Idle,
		Open,
		HalfClosedLocal,
		HalfClosedRemote,
		Closed,
	};

	// HTTP/2 스트림. IStream 위에서 동작하며 하나의 요청/응답 교환을 표현.
	class Http2Stream
	{
	public:
		explicit Http2Stream(uint32_t _id, ne::network::IStream& _transport) noexcept;
		Http2Stream(Http2Stream&&) noexcept = default;
		Http2Stream& operator=(Http2Stream&&) noexcept = default;
		~Http2Stream() = default;

		NEBULA_NON_COPYABLE(Http2Stream)

	private:
		uint32_t              streamId;
		StreamState           state{ StreamState::Idle };
		ne::network::IStream* transport;

	public:
		[[nodiscard]] uint32_t    Id()    const noexcept { return streamId; }
		[[nodiscard]] StreamState State() const noexcept { return state; }

	public:
		[[nodiscard]] ne::Task<ne::Result<void, ne::HttpError>>
		SendHeaders(const ne::network::HttpHeaders& _headers, bool_t _endStream,
		            const std::vector<ne::byte_t>& _hpackBlock);

		[[nodiscard]] ne::Task<ne::Result<void, ne::HttpError>>
		SendData(std::span<const ne::byte_t> _data, bool_t _endStream);

		[[nodiscard]] ne::Task<ne::Result<ne::network::HttpResponse, ne::HttpError>>
		ReceiveResponse(const ne::network::HttpHeaders& _responseHeaders);
	};
END_NS
