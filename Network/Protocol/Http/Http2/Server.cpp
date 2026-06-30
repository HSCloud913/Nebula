//
// Created by hscloud on 26. 6. 30.
//

#include "Server.h"
#include <array>
#include <vector>
#include <format>

BEGIN_NS(ne::network::http_2)
	Http2Server::Http2Server(std::unique_ptr<ne::network::IStream> _stream,
	                         Http2Handler _handler) noexcept
		: stream(std::move(_stream))
		, handler(std::move(_handler)) {}

	Http2Server::~Http2Server()
	{
		if (stream) (void)stream->Close();
	}

	ne::Task<ne::Result<void, ne::HttpError>> Http2Server::Run()
	{
		// 클라이언트 preface 확인
		std::vector<ne::byte_t> prefaceBuf(kClientPreface.size());
		std::size_t read = 0;
		while (read < kClientPreface.size())
		{
			auto r = co_await stream->Receive(
				BufferView{ nullptr, prefaceBuf.data() + read, kClientPreface.size() - read });
			if (r.IsError())
				co_return ne::Result<void, ne::HttpError>::Error(
					ne::HttpError{ r.Error().What() }.Context("[Http2Server/Run/ReadPreface]"));
			if (r.Value() == 0)
				co_return ne::Result<void, ne::HttpError>::Error(ne::HttpError{ "connection closed" });
			read += r.Value();
		}

		for (std::size_t i = 0; i < kClientPreface.size(); ++i)
		{
			if (prefaceBuf[i] != static_cast<ne::byte_t>(kClientPreface[i]))
				co_return ne::Result<void, ne::HttpError>::Error(
					ne::HttpError{ "invalid client preface" }.Context("[Http2Server/Run]"));
		}

		auto sp = co_await SendSettings();
		if (sp.IsError()) co_return sp;

		// 요청 루프
		while (true)
		{
			std::array<ne::byte_t, kFrameHeaderSize> hbuf{};
			std::size_t hr = 0;
			while (hr < kFrameHeaderSize)
			{
				auto r = co_await stream->Receive(
					BufferView{ nullptr, hbuf.data() + hr, kFrameHeaderSize - hr });
				if (r.IsError())
					co_return ne::Result<void, ne::HttpError>::Error(
						ne::HttpError{ r.Error().What() }.Context("[Http2Server/Run/FrameHeader]"));
				if (r.Value() == 0) co_return ne::Result<void, ne::HttpError>::Ok();
				hr += r.Value();
			}

			const auto fh = FrameHeader::Parse(hbuf.data());

			std::vector<ne::byte_t> payload(fh.length);
			std::size_t pr = 0;
			while (pr < fh.length)
			{
				auto r = co_await stream->Receive(
					BufferView{ nullptr, payload.data() + pr, fh.length - pr });
				if (r.IsError())
					co_return ne::Result<void, ne::HttpError>::Error(
						ne::HttpError{ r.Error().What() }.Context("[Http2Server/Run/Payload]"));
				pr += r.Value();
			}

			if (fh.type == FrameType::Settings && !(fh.flags & Flag::Ack))
			{
				std::array<ne::byte_t, kFrameHeaderSize> ack{};
				BuildFrameHeader(ack.data(), 0, FrameType::Settings, Flag::Ack, 0);
				(void)co_await stream->Send(BufferView{ nullptr, ack.data(), ack.size() });
				continue;
			}

			if (fh.type == FrameType::GoAway)
				co_return ne::Result<void, ne::HttpError>::Ok();

			if (fh.type == FrameType::Headers)
			{
				flowCtrl.InitStream(fh.streamId);
				auto hdrs = Hpack::Decode(payload.data(), payload.size());

				ne::network::HttpRequest req;
				for (const auto& [k, v] : hdrs)
				{
					if      (k == ":method") req.method = HttpMethod::GET;
					else if (k == ":path")   req.path   = v;
					else                     req.headers.emplace_back(k, v);
				}

				if (fh.flags & Flag::EndStream)
				{
					auto resp = co_await handler(std::move(req));
					auto shdr = co_await SendHeaders(fh.streamId, resp);
					if (shdr.IsError()) co_return shdr;
					if (!resp.body.empty())
					{
						auto sd = co_await SendData(fh.streamId, resp.body);
						if (sd.IsError()) co_return sd;
					}
					flowCtrl.RemoveStream(fh.streamId);
				}
			}
		}
	}

	ne::Task<ne::Result<void, ne::HttpError>> Http2Server::SendPreface()
	{
		co_return ne::Result<void, ne::HttpError>::Ok();
	}

	ne::Task<ne::Result<void, ne::HttpError>> Http2Server::SendSettings()
	{
		std::array<ne::byte_t, kFrameHeaderSize> frame{};
		BuildFrameHeader(frame.data(), 0, FrameType::Settings, 0, 0);
		auto r = co_await stream->Send(BufferView{ nullptr, frame.data(), frame.size() });
		if (r.IsError())
			co_return ne::Result<void, ne::HttpError>::Error(
				ne::HttpError{ r.Error().What() }.Context("[Http2Server/SendSettings]"));
		co_return ne::Result<void, ne::HttpError>::Ok();
	}

	ne::Task<ne::Result<void, ne::HttpError>>
	Http2Server::SendHeaders(uint32_t _streamId, const ne::network::HttpResponse& _resp)
	{
		const bool_t hasBody = !_resp.body.empty();
		const auto block = Hpack::Encode(
			"", "", "", "",
			{ { ":status", std::format("{}", _resp.status) } }
		);

		std::array<ne::byte_t, kFrameHeaderSize> hdr{};
		uint8_t flags = Flag::EndHeaders;
		if (!hasBody) flags |= Flag::EndStream;
		BuildFrameHeader(hdr.data(), static_cast<uint32_t>(block.size()), FrameType::Headers, flags, _streamId);

		auto r = co_await stream->Send(BufferView{ nullptr, hdr.data(), hdr.size() });
		if (r.IsError())
			co_return ne::Result<void, ne::HttpError>::Error(
				ne::HttpError{ r.Error().What() }.Context("[Http2Server/SendHeaders]"));

		if (!block.empty())
		{
			auto r2 = co_await stream->Send(BufferView{ nullptr, const_cast<ne::byte_t*>(block.data()), block.size() });
			if (r2.IsError())
				co_return ne::Result<void, ne::HttpError>::Error(
					ne::HttpError{ r2.Error().What() }.Context("[Http2Server/SendHeaders/Block]"));
		}

		co_return ne::Result<void, ne::HttpError>::Ok();
	}

	ne::Task<ne::Result<void, ne::HttpError>>
	Http2Server::SendData(uint32_t _streamId, const ne::string_t& _body)
	{
		const auto* body = reinterpret_cast<const ne::byte_t*>(_body.data());
		std::array<ne::byte_t, kFrameHeaderSize> hdr{};
		BuildFrameHeader(hdr.data(), static_cast<uint32_t>(_body.size()),
		                 FrameType::Data, Flag::EndStream, _streamId);

		auto r = co_await stream->Send(BufferView{ nullptr, hdr.data(), hdr.size() });
		if (r.IsError())
			co_return ne::Result<void, ne::HttpError>::Error(
				ne::HttpError{ r.Error().What() }.Context("[Http2Server/SendData/Header]"));

		auto r2 = co_await stream->Send(BufferView{ nullptr, const_cast<ne::byte_t*>(body), _body.size() });
		if (r2.IsError())
			co_return ne::Result<void, ne::HttpError>::Error(
				ne::HttpError{ r2.Error().What() }.Context("[Http2Server/SendData/Payload]"));

		co_return ne::Result<void, ne::HttpError>::Ok();
	}
END_NS
