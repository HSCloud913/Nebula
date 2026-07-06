//
// Created by hscloud on 26. 6. 30.
//

#include "Stream.h"
#include <array>
#include <vector>
#include <cstring>

BEGIN_NS(ne::network::http_2)
	Http2Stream::Http2Stream(uint32_t _id, ne::network::IStream& _transport) noexcept
		: streamId(_id)
		, transport(&_transport) {}

	ne::Task<ne::Result<void, ne::HttpError>>
	Http2Stream::SendHeaders(const ne::network::HttpHeaders& _headers, bool_t _endStream,
	                         const std::vector<ne::byte_t>& _hpackBlock)
	{
		(void)_headers;
		std::array<ne::byte_t, kFrameHeaderSize> header{};
		uint8_t flags = Flag::EndHeaders;
		if (_endStream) flags |= Flag::EndStream;
		BuildFrameHeader(header.data(),
		                 static_cast<uint32_t>(_hpackBlock.size()),
		                 FrameType::Headers,
		                 flags,
		                 streamId);

		auto r = co_await transport->Send(ne::io::BufferView{ nullptr, header.data(), header.size() });
		if (r.IsError())
			co_return ne::Result<void, ne::HttpError>::Error(
				ne::HttpError{ r.Error().What() }.Context("[Http2Stream/SendHeaders/Header]"));

		if (!_hpackBlock.empty())
		{
			auto r2 = co_await transport->Send(ne::io::BufferView{ nullptr, const_cast<ne::byte_t*>(_hpackBlock.data()), _hpackBlock.size() });
			if (r2.IsError())
				co_return ne::Result<void, ne::HttpError>::Error(
					ne::HttpError{ r2.Error().What() }.Context("[Http2Stream/SendHeaders/Block]"));
		}

		state = _endStream ? StreamState::HalfClosedLocal : StreamState::Open;
		co_return ne::Result<void, ne::HttpError>::Ok();
	}

	ne::Task<ne::Result<void, ne::HttpError>>
	Http2Stream::SendData(std::span<const ne::byte_t> _data, bool_t _endStream)
	{
		std::array<ne::byte_t, kFrameHeaderSize> header{};
		const uint8_t flags = _endStream ? Flag::EndStream : uint8_t{0};
		BuildFrameHeader(header.data(),
		                 static_cast<uint32_t>(_data.size()),
		                 FrameType::Data,
		                 flags,
		                 streamId);

		auto r = co_await transport->Send(ne::io::BufferView{ nullptr, header.data(), header.size() });
		if (r.IsError())
			co_return ne::Result<void, ne::HttpError>::Error(
				ne::HttpError{ r.Error().What() }.Context("[Http2Stream/SendData/Header]"));

		if (!_data.empty())
		{
			auto r2 = co_await transport->Send(ne::io::BufferView{ nullptr, const_cast<ne::byte_t*>(_data.data()), _data.size() });
			if (r2.IsError())
				co_return ne::Result<void, ne::HttpError>::Error(
					ne::HttpError{ r2.Error().What() }.Context("[Http2Stream/SendData/Payload]"));
		}

		if (_endStream) state = StreamState::HalfClosedLocal;
		co_return ne::Result<void, ne::HttpError>::Ok();
	}

	ne::Task<ne::Result<ne::network::HttpResponse, ne::HttpError>>
	Http2Stream::ReceiveResponse(const ne::network::HttpHeaders& _responseHeaders)
	{
		ne::network::HttpResponse resp;
		resp.headers = _responseHeaders;

		for (const auto& [k, v] : _responseHeaders)
		{
			if (k == ":status")
			{
				resp.status = static_cast<uint16_t>(std::stoi(v));
				break;
			}
		}

		// DATA 프레임 수집
		while (state != StreamState::HalfClosedRemote && state != StreamState::Closed)
		{
			std::array<ne::byte_t, kFrameHeaderSize> headerBuf{};
			std::size_t total = 0;
			while (total < kFrameHeaderSize)
			{
				auto r = co_await transport->Receive(
					ne::io::BufferView{ nullptr, headerBuf.data() + total, kFrameHeaderSize - total });
				if (r.IsError())
					co_return ne::Result<ne::network::HttpResponse, ne::HttpError>::Error(
						ne::HttpError{ r.Error().What() }.Context("[Http2Stream/ReceiveResponse/FrameHeader]"));
				if (r.Value() == 0)
					co_return ne::Result<ne::network::HttpResponse, ne::HttpError>::Error(
						ne::HttpError{ "connection closed" }.Context("[Http2Stream/ReceiveResponse]"));
				total += r.Value();
			}

			const auto fh = FrameHeader::Parse(headerBuf.data());
			if (fh.type == FrameType::Data)
			{
				std::vector<ne::byte_t> payload(fh.length);
				std::size_t read = 0;
				while (read < fh.length)
				{
					auto r = co_await transport->Receive(
						ne::io::BufferView{ nullptr, payload.data() + read, fh.length - read });
					if (r.IsError())
						co_return ne::Result<ne::network::HttpResponse, ne::HttpError>::Error(
							ne::HttpError{ r.Error().What() }.Context("[Http2Stream/ReceiveResponse/Data]"));
					read += r.Value();
				}
				resp.body.append(reinterpret_cast<const char*>(payload.data()), payload.size());
				if (fh.flags & Flag::EndStream)
				{
					state = StreamState::HalfClosedRemote;
					break;
				}
			}
			else if (fh.type == FrameType::RstStream)
			{
				state = StreamState::Closed;
				co_return ne::Result<ne::network::HttpResponse, ne::HttpError>::Error(
					ne::HttpError{ 0, "RST_STREAM received" }.Context("[Http2Stream/ReceiveResponse]"));
			}
			else
			{
				// 다른 프레임 타입은 무시하고 페이로드 소비
				std::vector<ne::byte_t> discard(fh.length);
				std::size_t dread = 0;
				while (dread < fh.length)
				{
					auto r = co_await transport->Receive(
						ne::io::BufferView{ nullptr, discard.data() + dread, fh.length - dread });
					if (r.IsError()) break;
					dread += r.Value();
				}
				if (fh.flags & Flag::EndStream)
				{
					state = StreamState::HalfClosedRemote;
					break;
				}
			}
		}

		co_return ne::Result<ne::network::HttpResponse, ne::HttpError>::Ok(std::move(resp));
	}
END_NS
