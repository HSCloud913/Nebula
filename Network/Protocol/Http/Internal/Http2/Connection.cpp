//
// Created by hscloud on 26. 7. 28.
//

#include "Network/Protocol/Http/Internal/Http2/Connection.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>
#include "Memory/Buffer/BufferView.h"
#include "Time/HttpDate.h"
#include "Network/Protocol/Http/Message/Method.h"
#include "Network/Protocol/Http/Message/Status.h"

namespace ne::network::http_2::internal
{
	namespace
	{
		constexpr std::size_t ReadChunk = 16384;

		string_t ToLowerAscii(const string_view_t _text)
		{
			string_t result;
			result.reserve(_text.size());
			for (const char c : _text) result.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
			return result;
		}

		// h2 에서 금지/변환되는 연결 특화 헤더(RFC 9113 §8.2.2). host 는 :authority 로 대체.
		bool_t IsSkippedHeader(const string_view_t _lowerName)
		{
			return _lowerName == "connection" || _lowerName == "keep-alive" || _lowerName == "proxy-connection" || _lowerName == "transfer-encoding" || _lowerName == "upgrade" || _lowerName == "host";
		}

		std::vector<byte_t> CollectBody(const http::Body& _body)
		{
			std::vector<byte_t> bytes;
			const auto view = _body.View();
			for (const auto& segment : view.Segments()) bytes.insert(bytes.end(), segment.ptr, segment.ptr + segment.length);
			return bytes;
		}

		int_t ParseStatus(const string_view_t _value)
		{
			int_t status = 0;
			std::from_chars(_value.data(), _value.data() + _value.size(), status);
			return status;
		}
	}



	// ───────────────────────── Connection (base) ─────────────────────────

	std::vector<SettingsEntry> Connection::LocalSettings() const
	{
		// 광고하지 않으면 피어는 프로토콜 기본값(MAX_FRAME_SIZE 16384 등)을 가정하는데, 우리 쪽에
		// 강제 로직이 없으면 그 가정을 어겨도 아무 일이 일어나지 않는다. 여기서 명시적으로 알리고
		// ReadFrame/HPACK 이 같은 값을 강제한다.
		return {
			{ SettingsId::ENABLE_PUSH, 0 }, // 서버 푸시 미지원
			{ SettingsId::MAX_FRAME_SIZE, localMaxFrameSize },
			{ SettingsId::MAX_HEADER_LIST_SIZE, static_cast<std::uint32_t>(localMaxHeaderListSize) },
		};
	}

	void_t Connection::ApplyPeerSettings(const RawFrame& _frame, std::int64_t& _windowDelta) noexcept
	{
		_windowDelta = 0;

		for (std::size_t offset = 0; offset + 6 <= _frame.payload.size(); offset += 6)
		{
			const auto id = static_cast<SettingsId>(ReadUint16(std::span<const byte_t>(_frame.payload).subspan(offset, 2)));
			const std::uint32_t value = ReadUint32(std::span<const byte_t>(_frame.payload).subspan(offset + 2, 4));

			switch (id)
			{
				case SettingsId::INITIAL_WINDOW_SIZE:
				{
					// 최대 2^31-1 을 넘는 값은 FLOW_CONTROL_ERROR 지만, 여기서는 클램프해 진행한다.
					const auto next = static_cast<std::int32_t>(value > 0x7FFFFFFFu ? 0x7FFFFFFFu : value);
					_windowDelta = static_cast<std::int64_t>(next) - static_cast<std::int64_t>(peerInitialWindow);
					peerInitialWindow = next;
					break;
				}
				case SettingsId::MAX_FRAME_SIZE:
					// 규격 허용 범위(16384~2^24-1)를 벗어난 값은 무시한다.
					if (value >= DefaultMaxFrameSize && value <= 0xFFFFFFu) peerMaxFrameSize = value;
					break;
				default:
					// HEADER_TABLE_SIZE 는 우리 인코더가 동적 테이블을 아예 쓰지 않으므로 영향이 없고,
					// ENABLE_PUSH/MAX_CONCURRENT_STREAMS/MAX_HEADER_LIST_SIZE 는 송신 측 제약이다.
					break;
			}
		}
	}

	ne::Task<ne::io::IoResult<bool_t>> Connection::FillAtLeast(const std::size_t _need, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<bool_t>;

		while (inbuf.size() - inpos < _need)
		{
			if (inpos > 0) // 소비된 앞부분을 회수해 버퍼가 무한히 커지지 않게 한다
			{
				inbuf.erase(inbuf.begin(), inbuf.begin() + inpos);
				inpos = 0;
			}

			const std::size_t old = inbuf.size();
			inbuf.resize(old + ReadChunk);

			auto received = co_await stream->Receive(ne::memory::BufferView{ inbuf.data() + old, ReadChunk }, _stopToken);
			if (received.IsError())
			{
				inbuf.resize(old);
				co_return R::Error(std::move(received.Error()));
			}

			const std::size_t got = received.Value();
			inbuf.resize(old + got);
			if (got == 0) co_return R::Ok(false); // EOF
		}

		co_return R::Ok(true);
	}

	ne::Task<ne::io::IoResult<std::optional<RawFrame>>> Connection::ReadFrame(std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::optional<RawFrame>>;

		auto header = co_await FillAtLeast(FrameHeaderSize, _stopToken);
		if (header.IsError()) co_return R::Error(std::move(header.Error()));
		if (!header.Value()) co_return R::Ok(std::nullopt); // 프레임 경계에서의 정상 EOF

		const auto parsed = ParseFrameHeader(std::span<const byte_t>(inbuf).subspan(inpos, FrameHeaderSize));
		const std::uint32_t length = parsed->length;

		// 길이 필드는 24비트라 최대 16MB 를 표현한다. 우리가 광고한 MAX_FRAME_SIZE 를 강제하지 않으면
		// 피어가 연결당 16MB 버퍼를 강제로 할당시킬 수 있다(RFC 9113 §4.2 상으로도 FRAME_SIZE_ERROR).
		if (length > localMaxFrameSize) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::INVALID_BUFFER, "peer frame exceeds advertised SETTINGS_MAX_FRAME_SIZE" }.Context("[Http2/ReadFrame]"));

		auto full = co_await FillAtLeast(FrameHeaderSize + length, _stopToken);
		if (full.IsError()) co_return R::Error(std::move(full.Error()));
		if (!full.Value()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "unexpected EOF mid-frame" });

		RawFrame frame;
		frame.header = *parsed;
		frame.payload.assign(inbuf.begin() + inpos + FrameHeaderSize, inbuf.begin() + inpos + FrameHeaderSize + length);
		inpos += FrameHeaderSize + length;

		co_return R::Ok(std::optional<RawFrame>(std::move(frame)));
	}

	ne::Task<ne::io::IoResult<void_t>> Connection::WriteRaw(std::vector<byte_t> _buffer, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<void_t>;

		co_await writeMutex.Lock();
		auto sent = co_await stream->Send(ne::memory::BufferView{ _buffer.data(), _buffer.size() }, _stopToken);
		writeMutex.Unlock();

		if (sent.IsError()) co_return R::Error(std::move(sent.Error()));
		co_return R::Ok();
	}

	ne::Task<ne::io::IoResult<void_t>> Connection::SendSettings(const bool_t _ack, std::stop_token _stopToken)
	{
		std::vector<byte_t> out;
		if (_ack) AppendSettingsAck(out);
		else
		{
			const auto settings = LocalSettings();
			AppendSettings(out, settings);
		}
		return WriteRaw(std::move(out), std::move(_stopToken));
	}

	ne::Task<ne::io::IoResult<void_t>> Connection::SendWindowUpdate(const std::uint32_t _streamId, const std::uint32_t _increment, std::stop_token _stopToken)
	{
		std::vector<byte_t> out;
		AppendWindowUpdate(out, _streamId, _increment);
		return WriteRaw(std::move(out), std::move(_stopToken));
	}

	ne::Task<ne::io::IoResult<void_t>> Connection::SendPingAck(const std::span<const byte_t> _opaque8, std::stop_token _stopToken)
	{
		std::vector<byte_t> out;
		AppendPing(out, _opaque8, true);
		return WriteRaw(std::move(out), std::move(_stopToken));
	}

	void_t Connection::Close()
	{
		if (stream) (void_t)stream->Close();
	}



	// ───────────────────────── ClientConnection ─────────────────────────

	HpackHeader ClientConnection::MakePseudo(const string_view_t _name, const string_view_t _value) const
	{
		return HpackHeader{ string_t(_name), string_t(_value) };
	}

	ne::Task<http::HttpResult<void_t>> ClientConnection::Start(std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		if (!IsOpen()) co_return R::Error(http::HttpError(http::HttpErrorKind::TRANSPORT, "stream not open").Context("[Http2Client/Start]"));

		std::vector<byte_t> out;
		out.insert(out.end(), ConnectionPreface.begin(), ConnectionPreface.end());
		const auto settings = LocalSettings();
		AppendSettings(out, settings);

		auto written = co_await WriteRaw(std::move(out), _stopToken);
		if (written.IsError()) co_return R::Error(http::HttpError(std::move(written.Error())).Context("[Http2Client/Start]"));

		driver.emplace(RunDriver());
		driver->Resume();

		co_return R::Ok();
	}

	void_t ClientConnection::FailAll(const http::HttpError& _error)
	{
		if (failReason.empty()) failReason = _error.What();

		std::vector<std::uint32_t> ids;
		ids.reserve(streams.size());
		for (auto& [id, stream] : streams)
		{
			stream->failed = true;
			ids.push_back(id);
		}

		// 재개는 루프에 예약(Post)한다 — 드라이버 스택 안에서 사용자 continuation 이 재진입 실행되는 것을 피한다.
		for (const std::uint32_t id : ids) if (const auto iter = streams.find(id); iter != streams.end()) iter->second->complete.SignalDeferred(context);
	}

	void_t ClientConnection::HandleDataOrHeaders(const RawFrame& _frame)
	{
		const auto iter = streams.find(_frame.header.streamId);
		if (iter == streams.end()) return; // 모르는/이미 끝난 스트림 → 무시

		Stream& stream = *iter->second;

		if (_frame.header.type == FrameType::HEADERS || _frame.header.type == FrameType::CONTINUATION)
		{
			stream.headerBlock.insert(stream.headerBlock.end(), _frame.payload.begin(), _frame.payload.end());
			if (_frame.header.type == FrameType::HEADERS && _frame.header.HasFlag(FLAG_END_STREAM)) stream.endStreamFlag = true;

			if (_frame.header.HasFlag(FLAG_END_HEADERS))
			{
				auto decoded = decoder.Decode(stream.headerBlock);
				stream.headerBlock.clear();
				if (!decoded)
				{
					failReason = "HPACK decode failed";
					stream.failed = true;
					stream.complete.SignalDeferred(context);
					return;
				}

				for (const auto& header : *decoded)
				{
					if (header.name == ":status") stream.statusCode = ParseStatus(header.value);
					else if (!header.name.empty() && header.name.front() == ':') continue; // 기타 pseudo 무시
					else stream.headers.Add(header.name, header.value);
				}
				stream.headersDone = true;

				// 스트리밍 sink: 헤더가 완성되는 즉시 onHead 를 1회 호출. false 반환 시 스트림 중단(RST 예약).
				if (stream.sink && stream.sink->onHead && !stream.sink->onHead(stream.statusCode, {}, stream.headers))
				{
					stream.done = true;
					pendingResets.push_back(_frame.header.streamId);
					stream.complete.SignalDeferred(context);
					return;
				}
			}
		}
		else // DATA
		{
			if (stream.sink) // 스트리밍 sink: 누적하지 않고 조각째 흘린다(span 은 콜백 동안만 유효).
			{
				if (stream.sink->onBody && !_frame.payload.empty() && !stream.sink->onBody(std::span<const byte_t>(_frame.payload)))
				{
					stream.done = true;
					pendingResets.push_back(_frame.header.streamId);
					stream.complete.SignalDeferred(context);
					return;
				}
			}
			else stream.body.insert(stream.body.end(), _frame.payload.begin(), _frame.payload.end());

			if (_frame.header.HasFlag(FLAG_END_STREAM))
			{
				stream.done = true;
				stream.complete.SignalDeferred(context);
				return;
			}
		}

		if (stream.headersDone && stream.endStreamFlag && !stream.done)
		{
			stream.done = true;
			stream.complete.SignalDeferred(context);
		}
	}

	ne::Task<void_t> ClientConnection::RunDriver()
	{
		co_await RunDriverLoop();
		driverFinished = true;

		// SignalDeferred: 동기 Signal 이면 DrainClose 대기자가 이 드라이버 스택 안에서 재개되어,
		// 그 연장선에서 connection(=이 프레임의 소유자)을 파괴할 수 있다 — 실행 중인 프레임의 자기
		// 파괴(UB). 지연 재개로 드라이버가 final_suspend 까지 완전히 물러난 다음 tick 에 깨운다.
		driverDone.SignalDeferred(context);
		co_return;
	}

	ne::Task<void_t> ClientConnection::DrainClose()
	{
		driverStop.request_stop();
		Close(); // 진행 중인 Receive/Send 완료를 취소로 깨운다

		if (driver && !driverFinished) co_await driverDone; // 드라이버가 완전히 끝날 때까지 대기(엔진 파괴 전 in-flight op 제거)
		co_return;
	}

	ne::Task<void_t> ClientConnection::RunDriverLoop()
	{
		const std::stop_token token = driverStop.get_token();

		while (!token.stop_requested())
		{
			auto result = co_await ReadFrame(token);
			if (result.IsError())
			{
				FailAll(http::HttpError(std::move(result.Error())));
				co_return;
			}
			if (!result.Value()) // EOF
			{
				FailAll(http::HttpError(http::HttpErrorKind::CONNECTION_CLOSED));
				co_return;
			}

			const RawFrame& frame = *result.Value();

			switch (frame.header.type)
			{
				case FrameType::SETTINGS:
				{
					if (frame.header.HasFlag(FLAG_ACK)) break;

					std::int64_t windowDelta = 0;
					ApplyPeerSettings(frame, windowDelta);

					// 이미 열린 스트림의 송신 윈도우도 차분으로 조정한다(RFC 9113 §6.9.2).
					if (windowDelta != 0)
					{
						for (auto& [id, slot] : streams)
						{
							slot->sendWindow += windowDelta;
							if (slot->sendWindow > 0) slot->windowReady.SignalDeferred(context);
						}
					}

					if (auto ack = co_await SendSettings(true, token); ack.IsError()) { FailAll(http::HttpError(std::move(ack.Error()))); co_return; }
					break;
				}
				case FrameType::WINDOW_UPDATE:
				{
					if (frame.payload.size() < 4) break;
					const std::uint32_t increment = ReadUint32(frame.payload) & 0x7FFFFFFFu;
					if (frame.header.streamId == 0)
					{
						connSendWindow += increment;
						for (auto& [id, stream] : streams) stream->windowReady.Signal();
					}
					else if (const auto iter = streams.find(frame.header.streamId); iter != streams.end())
					{
						iter->second->sendWindow += increment;
						iter->second->windowReady.Signal();
					}
					break;
				}
				case FrameType::PING:
				{
					if (!frame.header.HasFlag(FLAG_ACK)) if (auto pong = co_await SendPingAck(frame.payload, token); pong.IsError()) { FailAll(http::HttpError(std::move(pong.Error()))); co_return; }
					break;
				}
				case FrameType::GOAWAY:
				{
					goawayReceived = true;
					break;
				}
				case FrameType::RST_STREAM:
				{
					if (const auto iter = streams.find(frame.header.streamId); iter != streams.end())
					{
						failReason = "stream reset by peer";
						iter->second->failed = true;
						iter->second->complete.SignalDeferred(context);
					}
					break;
				}
				case FrameType::HEADERS:
				case FrameType::CONTINUATION:
				case FrameType::DATA:
				{
					const bool_t isData = frame.header.type == FrameType::DATA;
					const std::uint32_t length = frame.header.length;
					const std::uint32_t streamId = frame.header.streamId;
					HandleDataOrHeaders(frame);

					// HandleDataOrHeaders 가 스트림을 완결시키면 그 Send 코루틴이 이 자리(드라이버 스택)에서 재개된다.
					// 그 연장선에서 DrainClose 가 호출돼 stop+Close 가 걸릴 수 있으므로, 여기서 확인해 닫힌 스트림에
					// 후속 I/O(WINDOW_UPDATE/ReadFrame)를 시도하지 않도록 즉시 빠져나간다.
					if (token.stop_requested()) co_return;

					// sink 콜백 조기 중단으로 예약된 RST_STREAM 을 코루틴 컨텍스트인 여기서 보낸다.
					while (!pendingResets.empty())
					{
						const std::uint32_t rstId = pendingResets.back();
						pendingResets.pop_back();

						std::vector<byte_t> rst;
						AppendRstStream(rst, rstId, ErrorCode::CANCEL);
						if (auto written = co_await WriteRaw(std::move(rst), token); written.IsError()) { FailAll(http::HttpError(std::move(written.Error()))); co_return; }
					}

					if (isData && length > 0) // 소비한 만큼 흐름제어 윈도우 회복
					{
						if (auto w = co_await SendWindowUpdate(0, length, token); w.IsError()) { FailAll(http::HttpError(std::move(w.Error()))); co_return; }
						if (auto w = co_await SendWindowUpdate(streamId, length, token); w.IsError()) { FailAll(http::HttpError(std::move(w.Error()))); co_return; }
					}
					break;
				}
				default:
					break;
			}
		}

		co_return;
	}

	ne::Task<http::HttpResult<http::Response>> ClientConnection::Send(http::Request _request, std::stop_token _stopToken)
	{
		return SendImpl(std::move(_request), nullptr, std::move(_stopToken));
	}

	ne::Task<http::HttpResult<void_t>> ClientConnection::SendStreaming(http::Request _request, const http::ResponseCallbacks& _sink, std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		auto result = co_await SendImpl(std::move(_request), &_sink, std::move(_stopToken));
		if (result.IsError()) co_return R::Error(std::move(result.Error()));

		co_return R::Ok();
	}

	ne::Task<http::HttpResult<http::Response>> ClientConnection::SendImpl(http::Request _request, const http::ResponseCallbacks* _sink, std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Response>;

		if (!IsOpen() || goawayReceived) co_return R::Error(http::HttpError(http::HttpErrorKind::TRANSPORT, "connection not available").Context("[Http2Client/Send]"));

		const std::uint32_t id = nextStreamId;
		nextStreamId += 2;

		auto state = std::make_unique<Stream>();
		state->sendWindow = peerInitialWindow;
		state->sink = _sink;
		Stream* self = state.get();
		streams.emplace(id, std::move(state));

		const std::vector<byte_t> body = CollectBody(_request.body);
		const bool_t hasBody = !body.empty();

		// pseudo-header(순서 준수) + 일반 헤더
		HeaderList headers;
		headers.push_back(MakePseudo(":method", http::ToString(_request.method)));
		headers.push_back(MakePseudo(":scheme", scheme));
		headers.push_back(MakePseudo(":path", _request.target));
		headers.push_back(MakePseudo(":authority", authority));

		bool_t hasContentLength = false;
		for (const auto& [name, value] : _request.headers)
		{
			const string_t lower = ToLowerAscii(name);
			if (IsSkippedHeader(lower)) continue;
			if (lower == "content-length") hasContentLength = true;
			headers.push_back(HpackHeader{ lower, string_t(value) });
		}
		if (hasBody && !hasContentLength) headers.push_back(HpackHeader{ "content-length", std::to_string(body.size()) });

		std::vector<byte_t> block;
		encoder.Encode(headers, block);

		std::vector<byte_t> out;
		AppendHeaderBlock(out, id, block, !hasBody, peerMaxFrameSize);

		if (auto written = co_await WriteRaw(std::move(out), _stopToken); written.IsError())
		{
			streams.erase(id);
			co_return R::Error(http::HttpError(std::move(written.Error())).Context("[Http2Client/Send]"));
		}

		// 본문을 흐름제어 윈도우 안에서 DATA 프레임으로 전송
		std::size_t offset = 0;
		while (offset < body.size())
		{
			const std::int64_t available = std::min<std::int64_t>({ static_cast<std::int64_t>(peerMaxFrameSize), connSendWindow, self->sendWindow, static_cast<std::int64_t>(body.size() - offset) });
			if (available <= 0)
			{
				co_await self->windowReady; // 드라이버가 WINDOW_UPDATE 수신 시 신호
				continue;
			}

			const std::size_t chunk = static_cast<std::size_t>(available);
			const bool_t last = (offset + chunk) >= body.size();

			std::vector<byte_t> frame;
			AppendData(frame, id, std::span<const byte_t>(body).subspan(offset, chunk), last);
			if (auto written = co_await WriteRaw(std::move(frame), _stopToken); written.IsError())
			{
				streams.erase(id);
				co_return R::Error(http::HttpError(std::move(written.Error())).Context("[Http2Client/Send]"));
			}

			connSendWindow -= available;
			self->sendWindow -= available;
			offset += chunk;
		}

		if (!self->done && !self->failed) co_await self->complete;

		if (self->failed)
		{
			const string_t reason = failReason;
			streams.erase(id);
			co_return R::Error(http::HttpError(http::HttpErrorKind::TRANSPORT, reason.empty() ? string_view_t("stream failed") : string_view_t(reason)).Context("[Http2Client/Send]"));
		}

		http::Response response;
		response.statusCode = self->statusCode;
		response.headers = std::move(self->headers);
		response.body = http::Body(std::move(self->body));
		streams.erase(id);

		co_return R::Ok(std::move(response));
	}



	// ───────────────────────── ServerConnection ─────────────────────────

	ne::Task<void_t> ServerConnection::RunDispatch(const std::uint32_t _streamId, std::stop_token _stopToken)
	{
		// 스트림 하나의 처리 실패가 연결 전체를 끊지 않도록 에러는 삼킨다(h2 의 장점 — DispatchStream 이
		// 필요하면 해당 스트림만 RST_STREAM 한다).
		(void_t)co_await DispatchStream(_streamId, std::move(_stopToken));

		// SignalDeferred: Run() 은 깨어나면 이 태스크의 프레임을 회수하므로, 이 프레임이 완전히 끝난
		// 뒤(다음 tick) 재개되도록 지연 신호한다 — 실행 중인 프레임 파괴 방지.
		if (--activeDispatches == 0) dispatchesDone.SignalDeferred(context);
	}

	ne::Task<http::HttpResult<void_t>> ServerConnection::SendDataFlowControlled(const std::uint32_t _streamId, const std::span<const byte_t> _data, const bool_t _endStream, std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		const auto iterator = streams.find(_streamId);
		if (iterator == streams.end()) co_return R::Ok(); // RST 등으로 이미 사라진 스트림
		Stream& stream = *iterator->second;

		// 빈 본문이라도 END_STREAM 을 실은 DATA 프레임 하나는 보내야 스트림이 종결된다.
		if (_data.empty())
		{
			if (!_endStream) co_return R::Ok();

			std::vector<byte_t> frame;
			AppendData(frame, _streamId, {}, true);

			co_return (co_await WriteRaw(std::move(frame), _stopToken)).IsError() ? R::Error(http::HttpError(http::HttpErrorKind::TRANSPORT, "write failed").Context("[Http2Server/SendData]")) : R::Ok();
		}

		std::size_t offset = 0;
		while (offset < _data.size())
		{
			// 세 상한(프레임 크기 / 연결 윈도우 / 스트림 윈도우) 중 가장 작은 값만큼 보낼 수 있다.
			const std::int64_t available = std::min<std::int64_t>({ static_cast<std::int64_t>(peerMaxFrameSize), connSendWindow, stream.sendWindow, static_cast<std::int64_t>(_data.size() - offset) });
			if (available <= 0)
			{
				co_await stream.windowReady; // 드라이버가 WINDOW_UPDATE 수신 시 신호
				continue;
			}

			const auto chunk = static_cast<std::size_t>(available);
			const bool_t last = _endStream && (offset + chunk) >= _data.size();

			std::vector<byte_t> frame;
			AppendData(frame, _streamId, _data.subspan(offset, chunk), last);
			if (auto written = co_await WriteRaw(std::move(frame), _stopToken); written.IsError()) co_return R::Error(http::HttpError(std::move(written.Error())).Context("[Http2Server/SendData]"));

			connSendWindow -= available;
			stream.sendWindow -= available;
			offset += chunk;
		}

		co_return R::Ok();
	}

	ne::Task<http::HttpResult<void_t>> ServerConnection::SendStreamingBody(const std::uint32_t _streamId, const http::BodyProducer& _producer, std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		while (true)
		{
			auto chunk = co_await _producer();
			if (chunk.IsError())
			{
				// 본문 중간 생산 실패 — 이 스트림만 리셋하고 연결은 유지한다(h2 의 장점).
				std::vector<byte_t> rst;
				AppendRstStream(rst, _streamId, ErrorCode::INTERNAL_ERROR);
				(void_t)co_await WriteRaw(std::move(rst), _stopToken);
				co_return R::Ok();
			}

			const std::vector<byte_t>& data = chunk.Value();
			if (data.empty()) // EOF — 빈 DATA 프레임에 END_STREAM 을 실어 종결
			{
				co_return co_await SendDataFlowControlled(_streamId, {}, true, std::move(_stopToken));
			}

			if (auto sent = co_await SendDataFlowControlled(_streamId, data, false, _stopToken); sent.IsError()) co_return R::Error(std::move(sent.Error()).Context("[Http2Server/SendStreamingBody]"));
		}
	}

	ne::Task<http::HttpResult<void_t>> ServerConnection::SendResponse(const std::uint32_t _streamId, http::Response _response, std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		const bool_t streaming = _response.body.IsStreaming();
		const std::vector<byte_t> body = streaming ? std::vector<byte_t>{} : CollectBody(_response.body);
		const bool_t hasBody = streaming || !body.empty();

		HeaderList headers;
		headers.push_back(HpackHeader{ ":status", std::to_string(_response.statusCode) });

		bool_t hasContentLength = false;
		bool_t hasDate = false;
		for (const auto& [name, value] : _response.headers)
		{
			const string_t lower = ToLowerAscii(name);
			if (IsSkippedHeader(lower)) continue;
			if (lower == "content-length") hasContentLength = true;
			if (lower == "date") hasDate = true;
			headers.push_back(HpackHeader{ lower, string_t(value) });
		}

		// RFC 9110 §6.6.1: 시계를 가진 오리진 서버는 Date 를 보내야 한다(HTTP/1.1 경로와 동일).
		if (!hasDate) headers.push_back(HpackHeader{ "date", ne::time::FormatHttpDate(std::chrono::system_clock::now()) });
		if (!streaming && hasBody && !hasContentLength) headers.push_back(HpackHeader{ "content-length", std::to_string(body.size()) }); // 스트리밍은 크기 미상

		std::vector<byte_t> block;
		encoder.Encode(headers, block);

		std::vector<byte_t> out;
		AppendHeaderBlock(out, _streamId, block, !hasBody, peerMaxFrameSize);
		if (auto written = co_await WriteRaw(std::move(out), _stopToken); written.IsError()) co_return R::Error(http::HttpError(std::move(written.Error())).Context("[Http2Server/SendResponse]"));

		if (streaming) co_return co_await SendStreamingBody(_streamId, *_response.body.Producer(), std::move(_stopToken));

		// 본문이 없으면 위 HEADERS 에 이미 END_STREAM 이 실렸다 — 여기서 빈 DATA 를 더 보내면 이미 닫힌
		// 스트림에 프레임을 쓰는 프로토콜 위반이 된다.
		if (hasBody) if (auto sent = co_await SendDataFlowControlled(_streamId, body, true, _stopToken); sent.IsError()) co_return R::Error(std::move(sent.Error()).Context("[Http2Server/SendResponse]"));

		co_return R::Ok();
	}

	ne::Task<http::HttpResult<void_t>> ServerConnection::DispatchStream(const std::uint32_t _streamId, std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		const auto iter = streams.find(_streamId);
		if (iter == streams.end()) co_return R::Ok();

		Stream& stream = *iter->second;

		http::Request request;
		request.method = stream.method;
		request.target = std::move(stream.path);
		request.headers = std::move(stream.headers);
		request.body = http::Body(std::move(stream.body));
		if (!stream.authority.empty() && !request.headers.Has("Host")) request.headers.Set("Host", stream.authority);

		auto handled = co_await handler(request);
		http::Response response = handled.IsError() ? http::Response::Status(500) : std::move(handled.Value());

		auto sent = co_await SendResponse(_streamId, std::move(response), _stopToken);
		streams.erase(_streamId);

		co_return sent;
	}

	ne::Task<http::HttpResult<void_t>> ServerConnection::Run(std::stop_token _stopToken)
	{
		// 스트림 디스패치가 별도 태스크로 도는 만큼, 어떤 경로로 루프를 빠져나가든 그것들이 전부 끝난
		// 뒤에 반환해야 한다 — 진행 중인 디스패치를 문 채 반환하면 호출자가 이 연결을 파괴할 때 그
		// 태스크들이 파괴된 객체를 참조한다.
		std::stop_source dispatchStop;
		std::stop_callback forwardStop{ _stopToken, [&dispatchStop] { dispatchStop.request_stop(); } };

		auto result = co_await RunFrameLoop(dispatchStop.get_token());

		dispatchStop.request_stop();
		while (activeDispatches > 0) co_await dispatchesDone;

		co_return result;
	}

	ne::Task<http::HttpResult<void_t>> ServerConnection::RunFrameLoop(std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		// 클라이언트 연결 preface 확인
		auto preface = co_await FillAtLeast(ConnectionPreface.size(), _stopToken);
		if (preface.IsError()) co_return R::Error(http::HttpError(std::move(preface.Error())).Context("[Http2Server/Run]"));
		if (!preface.Value()) co_return R::Error(http::HttpError(http::HttpErrorKind::CONNECTION_CLOSED, "no preface").Context("[Http2Server/Run]"));
		if (std::memcmp(inbuf.data() + inpos, ConnectionPreface.data(), ConnectionPreface.size()) != 0) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "invalid connection preface").Context("[Http2Server/Run]"));
		inpos += ConnectionPreface.size();

		if (auto settings = co_await SendSettings(false, _stopToken); settings.IsError()) co_return R::Error(http::HttpError(std::move(settings.Error())).Context("[Http2Server/Run]"));

		while (!_stopToken.stop_requested())
		{
			auto result = co_await ReadFrame(_stopToken);
			if (result.IsError()) co_return R::Error(http::HttpError(std::move(result.Error())).Context("[Http2Server/Run]"));
			if (!result.Value()) co_return R::Ok(); // 피어가 연결을 닫음

			const RawFrame& frame = *result.Value();

			switch (frame.header.type)
			{
				case FrameType::SETTINGS:
				{
					if (frame.header.HasFlag(FLAG_ACK)) break;

					// 예전에는 ACK 만 보내고 피어 SETTINGS 를 **완전히 무시**했다 — 그래서 클라이언트가
					// 알려준 INITIAL_WINDOW_SIZE/MAX_FRAME_SIZE 가 아무 효과도 없었다.
					std::int64_t windowDelta = 0;
					ApplyPeerSettings(frame, windowDelta);

					// INITIAL_WINDOW_SIZE 변경은 이미 열린 스트림에도 차분으로 반영해야 한다(RFC 9113 §6.9.2).
					if (windowDelta != 0)
					{
						for (auto& [id, slot] : streams)
						{
							slot->sendWindow += windowDelta;
							if (slot->sendWindow > 0) slot->windowReady.SignalDeferred(context);
						}
					}

					if (auto ack = co_await SendSettings(true, _stopToken); ack.IsError()) co_return R::Error(http::HttpError(std::move(ack.Error())));
					break;
				}
				case FrameType::PING:
				{
					if (!frame.header.HasFlag(FLAG_ACK)) if (auto pong = co_await SendPingAck(frame.payload, _stopToken); pong.IsError()) co_return R::Error(http::HttpError(std::move(pong.Error())));
					break;
				}
				case FrameType::WINDOW_UPDATE:
				{
					if (frame.payload.size() < 4) break;

					const std::int64_t increment = ReadUint32(frame.payload) & 0x7FFFFFFFu;
					if (frame.header.streamId == 0)
					{
						connSendWindow += increment;

						// 연결 윈도우가 열리면 그것을 기다리던 모든 스트림을 깨워야 한다.
						for (auto& [id, slot] : streams) slot->windowReady.SignalDeferred(context);
						break;
					}

					// 스트림 레벨 WINDOW_UPDATE 를 예전에는 통째로 버렸다 — 그래서 초기 윈도우를 소진한
					// 뒤 피어가 창을 열어줘도 서버가 그 사실을 몰랐다.
					if (const auto iterator = streams.find(frame.header.streamId); iterator != streams.end())
					{
						iterator->second->sendWindow += increment;
						iterator->second->windowReady.SignalDeferred(context);
					}
					break;
				}
				case FrameType::GOAWAY:
				{
					goaway = true;
					break;
				}
				case FrameType::RST_STREAM:
				{
					streams.erase(frame.header.streamId);
					break;
				}
				case FrameType::HEADERS:
				case FrameType::CONTINUATION:
				case FrameType::DATA:
				{
					const std::uint32_t streamId = frame.header.streamId;
					auto& slot = streams[streamId];
					if (!slot) slot = std::make_unique<Stream>();
					Stream& stream = *slot;

					if (frame.header.type == FrameType::HEADERS || frame.header.type == FrameType::CONTINUATION)
					{
						stream.headerBlock.insert(stream.headerBlock.end(), frame.payload.begin(), frame.payload.end());

						// 헤더 블록 크기 상한 — HPACK 폭탄 등은 연결 자체를 신뢰할 수 없으므로 연결 에러로 처리.
						if (stream.headerBlock.size() > limits.maxHeaderBytes) co_return R::Error(http::HttpError(http::HttpErrorKind::HEADER_TOO_LARGE).Context("[Http2Server/Run]"));

						if (frame.header.HasFlag(FLAG_END_STREAM)) stream.endStream = true;

						if (frame.header.HasFlag(FLAG_END_HEADERS))
						{
							auto decoded = decoder.Decode(stream.headerBlock);
							stream.headerBlock.clear();
							if (!decoded) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "HPACK decode failed").Context("[Http2Server/Run]"));

							for (const auto& header : *decoded)
							{
								if (header.name == ":method") stream.method = http::MethodFromString(header.value);
								else if (header.name == ":path") stream.path = header.value;
								else if (header.name == ":authority") stream.authority = header.value;
								else if (!header.name.empty() && header.name.front() == ':') continue;
								else stream.headers.Add(header.name, header.value);
							}
							stream.headersDone = true;
						}
					}
					else // DATA
					{
						if (frame.header.HasFlag(FLAG_END_STREAM)) stream.endStream = true;

						// 본문 크기 상한 — 초과 스트림만 RST_STREAM 으로 거부하고 연결은 유지한다.
						if (!stream.rejected && stream.body.size() + frame.payload.size() > limits.maxBodyBytes)
						{
							stream.rejected = true;
							stream.body.clear();

							std::vector<byte_t> rst;
							AppendRstStream(rst, streamId, ErrorCode::CANCEL);
							if (auto written = co_await WriteRaw(std::move(rst), _stopToken); written.IsError()) co_return R::Error(http::HttpError(std::move(written.Error())));
						}
						if (!stream.rejected) stream.body.insert(stream.body.end(), frame.payload.begin(), frame.payload.end());

						if (frame.header.length > 0) // 거부된 스트림도 연결 레벨 윈도우는 회복해야 한다
						{
							if (auto w = co_await SendWindowUpdate(0, frame.header.length, _stopToken); w.IsError()) co_return R::Error(http::HttpError(std::move(w.Error())));
							if (auto w = co_await SendWindowUpdate(streamId, frame.header.length, _stopToken); w.IsError()) co_return R::Error(http::HttpError(std::move(w.Error())));
						}
					}

					if (stream.headersDone && stream.endStream)
					{
						if (stream.rejected) { streams.erase(streamId); } // 이미 RST 로 거부 — 디스패치 없이 정리
						else if (limits.maxConcurrentStreams > 0 && activeDispatches >= limits.maxConcurrentStreams)
						{
							// 동시 스트림 상한 초과 — REFUSED_STREAM 은 클라이언트가 안전하게 재시도할 수 있다.
							streams.erase(streamId);

							std::vector<byte_t> rst;
							AppendRstStream(rst, streamId, ErrorCode::REFUSED_STREAM);
							if (auto written = co_await WriteRaw(std::move(rst), _stopToken); written.IsError()) co_return R::Error(http::HttpError(std::move(written.Error())));
						}
						else
						{
							// 디스패치를 **별도 태스크로 분리**한다. 그래야 핸들러가 대기하는 동안에도 이
							// 루프가 계속 프레임을 읽어 다른 스트림을 진행시키고 WINDOW_UPDATE 를 회수한다.
							std::erase_if(dispatchTasks, [](const ne::Task<void_t>& _task) { return _task.IsReady(); });

							++activeDispatches;
							dispatchTasks.push_back(RunDispatch(streamId, _stopToken));
							dispatchTasks.back().Resume();
						}
					}
					break;
				}
				default:
					break;
			}
		}

		co_return R::Ok();
	}
}
