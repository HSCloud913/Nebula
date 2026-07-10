//
// Created by hscloud on 26. 6. 30.
//

#include "Network/Protocol/Http/Http2/Client.h"
#include "Network/Protocol/Http/Http2/Stream.h"
#include <array>
#include <vector>
#include <cstring>
#include <format>

BEGIN_NS (ne::network::http_2)
Http2Client::Http2Client(std::unique_ptr<ne::network::IStream> _stream) noexcept
	: stream(std::move(_stream)) {}

Http2Client::~Http2Client() { if (stream) (void)stream->Close(); }

ne::Task<ne::Result<Http2Client, ne::HttpError>> Http2Client::Connect(std::unique_ptr<ne::network::IStream> _stream)
{
	Http2Client client(std::move(_stream));

	auto pr = co_await client.SendPreface();
	if (pr.IsError()) co_return ne::Result < Http2Client, ne::HttpError > ::Error(std::move(pr.Error()));

	auto sr = co_await client.SendSettings();
	if (sr.IsError()) co_return ne::Result < Http2Client, ne::HttpError > ::Error(std::move(sr.Error()));

	co_return ne::Result < Http2Client, ne::HttpError > ::Ok(std::move(client));
}

ne::Task<ne::Result<ne::network::HttpResponse, ne::HttpError>> Http2Client::Request(const ne::network::HttpRequest& _req)
{
	const uint32_t sid = nextStreamId.fetch_add(2, std::memory_order_relaxed);
	flowCtrl.InitStream(sid);

	static constexpr std::string_view kMethodStr[] = { "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS" };
	const auto methodStr = kMethodStr[static_cast<std::size_t>(_req.method)];
	const auto hpackBlock = Hpack::Encode(methodStr, _req.path, "https", "", _req.headers);

	Http2Stream s2(sid, *stream);

	const bool_t hasBody = !_req.body.empty();

	auto hr = co_await s2.SendHeaders(_req.headers, !hasBody, hpackBlock);
	if (hr.IsError()) co_return ne::Result<ne::network::HttpResponse, ne::HttpError>::Error(std::move(hr.Error()));

	if (hasBody)
	{
		const auto* body = reinterpret_cast<const ne::byte_t*>(_req.body.data());
		auto dr = co_await s2.SendData(std::span<const ne::byte_t>(body, _req.body.size()), true);
		if (dr.IsError()) co_return ne::Result<ne::network::HttpResponse, ne::HttpError>::Error(std::move(dr.Error()));
	}

	// SETTINGS ACK 와 HEADERS 프레임 소비
	auto headersResult = co_await ReadHeaders();
	if (headersResult.IsError()) co_return ne::Result<ne::network::HttpResponse, ne::HttpError>::Error(std::move(headersResult.Error()));

	auto respResult = co_await s2.ReceiveResponse(headersResult.Value());
	co_return std::move(respResult);
}

ne::Task<ne::Result<void, ne::HttpError>> Http2Client::SendPreface()
{
	const auto pref = reinterpret_cast<const ne::byte_t*>(kClientPreface.data());
	auto r = co_await stream->Send(ne::io::BufferView{ nullptr, const_cast<ne::byte_t*>(pref), kClientPreface.size() });
	if (r.IsError())
		co_return ne::Result<void, ne::HttpError>::Error(ne::HttpError{ r.Error().What() }.Context("[Http2Client/SendPreface]"));
	co_return ne::Result<void, ne::HttpError>::Ok();
}

ne::Task<ne::Result<void, ne::HttpError>> Http2Client::SendSettings()
{
	// SETTINGS フレーム (payload 없음 — 기본값 사용)
	std::array<ne::byte_t, kFrameHeaderSize> frame{};
	BuildFrameHeader(frame.data(), 0, FrameType::Settings, 0, 0);

	auto r = co_await stream->Send(ne::io::BufferView{ nullptr, frame.data(), frame.size() });
	if (r.IsError())
		co_return ne::Result<void, ne::HttpError>::Error(ne::HttpError{ r.Error().What() }.Context("[Http2Client/SendSettings]"));
	co_return ne::Result<void, ne::HttpError>::Ok();
}

ne::Task<ne::Result<void, ne::HttpError>> Http2Client::SendWindowUpdate(uint32_t _streamId, int32_t _increment)
{
	std::array<ne::byte_t, kFrameHeaderSize + 4> frame{};
	BuildFrameHeader(frame.data(), 4, FrameType::WindowUpdate, 0, _streamId);
	WriteU32(frame.data() + kFrameHeaderSize, static_cast<uint32_t>(_increment) & 0x7FFFFFFFu);

	auto r = co_await stream->Send(ne::io::BufferView{ nullptr, frame.data(), frame.size() });
	if (r.IsError())
		co_return ne::Result<void, ne::HttpError>::Error(ne::HttpError{ r.Error().What() }.Context("[Http2Client/SendWindowUpdate]"));
	co_return ne::Result<void, ne::HttpError>::Ok();
}

ne::Task<ne::Result<ne::network::HttpHeaders, ne::HttpError>> Http2Client::ReadHeaders()
{
	// 프레임 헤더 루프 — SETTINGS_ACK, WINDOW_UPDATE 등을 건너뛰고 HEADERS 프레임을 반환
	while (true)
	{
		std::array<ne::byte_t, kFrameHeaderSize> hbuf{};
		std::size_t read = 0;
		while (read < kFrameHeaderSize)
		{
			auto r = co_await stream->Receive(ne::io::BufferView{ nullptr, hbuf.data() + read, kFrameHeaderSize - read });
			if (r.IsError())
				co_return ne::Result<ne::network::HttpHeaders, ne::HttpError>::Error(ne::HttpError{ r.Error().What() }.Context("[Http2Client/ReadHeaders]"));
			if (r.Value() == 0)
				co_return ne::Result<ne::network::HttpHeaders, ne::HttpError>::Error(ne::HttpError{ "connection closed" });
			read += r.Value();
		}

		const auto fh = FrameHeader::Parse(hbuf.data());

		if (fh.type == FrameType::Headers)
		{
			std::vector<ne::byte_t> block(fh.length);
			std::size_t blockRead = 0;
			while (blockRead < fh.length)
			{
				auto r = co_await stream->Receive(ne::io::BufferView{ nullptr, block.data() + blockRead, fh.length - blockRead });
				if (r.IsError())
					co_return ne::Result<ne::network::HttpHeaders, ne::HttpError>::Error(ne::HttpError{ r.Error().What() }.Context("[Http2Client/ReadHeaders/Block]"));
				blockRead += r.Value();
			}
			co_return ne::Result<ne::network::HttpHeaders, ne::HttpError>::Ok(Hpack::Decode(block.data(), block.size()));
		}

		// 다른 프레임은 페이로드 소비 후 다음 프레임으로
		std::vector<ne::byte_t> discard(fh.length);
		std::size_t discarded = 0;
		while (discarded < fh.length)
		{
			auto r = co_await stream->Receive(ne::io::BufferView{ nullptr, discard.data() + discarded, fh.length - discarded });
			if (r.IsError()) break;
			discarded += r.Value();
		}

		if (fh.type == FrameType::Settings && !(fh.flags & Flag::Ack))
		{
			// SETTINGS 에 ACK 응답
			std::array<ne::byte_t, kFrameHeaderSize> ack{};
			BuildFrameHeader(ack.data(), 0, FrameType::Settings, Flag::Ack, 0);
			(void)co_await stream->Send(ne::io::BufferView{ nullptr, ack.data(), ack.size() });
		}
	}
}
END_NS
