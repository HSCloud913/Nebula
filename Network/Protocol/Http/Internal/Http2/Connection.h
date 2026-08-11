//
// Created by hscloud on 26. 7. 28.
//

#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <unordered_map>
#include <vector>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Network/Stream/IStream.h"
#include "Network/Protocol/Http/Message/Request.h"
#include "Network/Protocol/Http/Message/Response.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"
#include "Network/Protocol/Http/Limits.h"
#include "Network/Protocol/Http/Message/Method.h"
#include "Network/Protocol/Http/ResponseCallbacks.h"
#include "Network/Protocol/Http/Internal/Http2/Frame.h"
#include "Network/Protocol/Http/Internal/Http2/Hpack.h"
#include "Network/Protocol/Http/Internal/Http2/Async.h"

namespace ne::network::http_2::internal
{
	/** @class RawFrame @brief 파싱된 프레임 하나(헤더 + 페이로드 복사본). */
	struct RawFrame
	{
		FrameHeader header;
		std::vector<byte_t> payload;
	};

	/**
	 * @class Connection
	 * @brief HTTP/2 연결의 공통 하부 계층 — 프레임 읽기/쓰기, HPACK, 흐름제어 상태를 담습니다(클라이언트/서버 공용 base).
	 *
	 * @note this 를 캡처하는 드라이버 코루틴이 붙으므로 이동/복사 불가하며, 항상 heap 에 고정해(unique_ptr) 사용합니다.
	 */
	class Connection
	{
	public:
		explicit Connection(std::unique_ptr<ne::network::IStream> _stream, ne::io::Context& _context) noexcept
			: stream(std::move(_stream))
			, context(_context) {}

		virtual ~Connection() = default;

		NEBULA_NON_COPYABLE_MOVABLE(Connection)

	protected:
		std::unique_ptr<ne::network::IStream> stream;
		ne::io::Context& context;

		AsyncMutex writeMutex;   // 프레임 쓰기 직렬화
		HpackEncoder encoder;
		HpackDecoder decoder;

		std::vector<byte_t> inbuf;      // 수신 버퍼
		std::size_t inpos{ 0 }; // 소비 위치

		std::uint32_t peerMaxFrameSize{ DefaultMaxFrameSize };
		std::int32_t peerInitialWindow{ DefaultInitialWindowSize };
		std::int64_t connSendWindow{ DefaultInitialWindowSize }; // 연결 레벨 송신 윈도우

	protected:
		// inbuf 에 (inpos 기준) 최소 _need 바이트를 확보. false=EOF.
		[[nodiscard]] ne::Task<ne::io::IoResult<bool_t>> FillAtLeast(std::size_t _need, std::stop_token _stopToken);
		// 프레임 하나 읽기. nullopt=프레임 경계에서의 정상 EOF.
		[[nodiscard]] ne::Task<ne::io::IoResult<std::optional<RawFrame>>> ReadFrame(std::stop_token _stopToken);
		// 준비된 바이트열을 writeMutex 하에 전부 전송.
		[[nodiscard]] ne::Task<ne::io::IoResult<void_t>> WriteRaw(std::vector<byte_t> _buffer, std::stop_token _stopToken);

		// 제어 프레임 편의 송신.
		[[nodiscard]] ne::Task<ne::io::IoResult<void_t>> SendSettings(bool_t _ack, std::stop_token _stopToken);
		[[nodiscard]] ne::Task<ne::io::IoResult<void_t>> SendWindowUpdate(std::uint32_t _streamId, std::uint32_t _increment, std::stop_token _stopToken);
		[[nodiscard]] ne::Task<ne::io::IoResult<void_t>> SendPingAck(std::span<const byte_t> _opaque8, std::stop_token _stopToken);

		// 우리 쪽 로컬 SETTINGS(INITIAL_WINDOW_SIZE 등). 서브클래스가 채운다.
		[[nodiscard]] static std::vector<SettingsEntry> LocalSettings();

	public:
		[[nodiscard]] bool_t IsOpen() const noexcept { return stream != nullptr && stream->IsOpen(); }
		void_t Close();
	};



	/**
	 * @class ClientConnection
	 * @brief HTTP/2 클라이언트 연결 — 하나의 전송 위에서 여러 요청을 스트림으로 멀티플렉싱합니다.
	 *
	 * Start() 로 preface + SETTINGS 를 보내고 백그라운드 드라이버(RunDriver)를 띄운 뒤, Send(request) 를
	 * 여러 번 호출하면 각 요청이 새 스트림을 열어 동시에 진행됩니다. 응답은 드라이버가 스트림 ID로 라우팅해
	 * 해당 Send 코루틴을 깨웁니다.
	 */
	class ClientConnection final :public Connection
	{
	public:
		ClientConnection(std::unique_ptr<ne::network::IStream> _stream, ne::io::Context& _context, string_t _authority, bool_t _isSecure) noexcept
			: Connection(std::move(_stream), _context)
			, authority(std::move(_authority))
			, scheme(_isSecure ? "https" : "http") {}

		~ClientConnection() override = default;

	private:
		struct Stream
		{
			int_t statusCode{ 0 };
			http::Headers headers;
			std::vector<byte_t> body;
			std::vector<byte_t> headerBlock; // HEADERS+CONTINUATION 조립
			bool_t headersDone{ false };
			bool_t endStreamFlag{ false }; // HEADERS 프레임에 END_STREAM 이 있었음(본문 없음)
			bool_t done{ false };
			bool_t failed{ false };
			std::int64_t sendWindow{ DefaultInitialWindowSize };
			ne::Event complete;    // done/failed 시 신호
			ne::Event windowReady; // 송신 윈도우 갱신 시 신호

			// 스트리밍 수신 sink(SendStreaming 프레임 소유). 설정 시 body 에 누적하지 않고 조각째 콜백으로 흘린다.
			const http::ResponseCallbacks* sink{ nullptr };
		};

		string_t authority;
		string_t scheme;
		std::uint32_t nextStreamId{ 1 };
		std::unordered_map<std::uint32_t, std::unique_ptr<Stream>> streams;
		std::vector<std::uint32_t> pendingResets; // sink 콜백 조기 중단 스트림 — 드라이버가 RST_STREAM(CANCEL) 송신
		bool_t goawayReceived{ false };
		string_t failReason;
		std::optional<ne::Task<void_t>> driver;
		std::stop_source driverStop;
		ne::Event driverDone;             // 드라이버 코루틴이 종료될 때 신호
		bool_t driverFinished{ false };

	public:
		/** @brief preface + SETTINGS 전송 후 드라이버를 기동합니다. Send 전에 1회 호출. */
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> Start(std::stop_token _stopToken);

		/** @brief _request 를 새 스트림으로 보내고 응답을 받습니다(멀티플렉싱). */
		[[nodiscard]] ne::Task<http::HttpResult<http::Response>> Send(http::Request _request, std::stop_token _stopToken);

		/**
		 * @brief _request 를 보내고 응답을 _sink 로 조각째 흘려 받습니다(본문 전체 버퍼링 없음).
		 * @note _sink 는 완료까지 호출자가 수명을 보장해야 합니다. 콜백이 false 를 반환하면 해당 스트림을
		 *       RST_STREAM(CANCEL) 로 중단하고 Ok 로 반환합니다(연결은 유지).
		 */
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> SendStreaming(http::Request _request, const http::ResponseCallbacks& _sink, std::stop_token _stopToken);

		/**
		 * @brief 스트림을 닫아 드라이버의 진행 중인 I/O 를 취소하고, 드라이버가 완전히 끝날 때까지 기다립니다.
		 * @note 연결/엔진을 파괴하기 전에 이걸로 드라이버를 정리해야 합니다 — in-flight op 를 문 채 엔진이
		 *       소멸하면 완료가 파괴된 엔진으로 배달되어 크래시할 수 있습니다. 이 연결을 구동하는 루프
		 *       (BlockOn/RunOnce) 위에서 co_await 해야 드라이버가 완료 신호까지 진행됩니다.
		 *       완료 신호는 지연 재개(SignalDeferred)라 드라이버 프레임이 완전히 물러난 뒤 깨어나므로,
		 *       co_await 가 반환된 직후 이 연결을 파괴해도 안전합니다.
		 */
		[[nodiscard]] ne::Task<void_t> DrainClose();

	private:
		// Send/SendStreaming 공용 구현 — _sink 가 있으면 스트림 상태에 연결해 드라이버가 조각째 흘린다.
		[[nodiscard]] ne::Task<http::HttpResult<http::Response>> SendImpl(http::Request _request, const http::ResponseCallbacks* _sink, std::stop_token _stopToken);

		[[nodiscard]] ne::Task<void_t> RunDriver();     // RunDriverLoop 를 감싸 종료 시 driverDone 을 신호
		[[nodiscard]] ne::Task<void_t> RunDriverLoop(); // 실제 프레임 읽기/디스패치 루프
		void_t HandleDataOrHeaders(const RawFrame& _frame);
		void_t FailAll(const http::HttpError& _error);
		[[nodiscard]] HpackHeader MakePseudo(string_view_t _name, string_view_t _value) const;
	};



	using Http2Handler = std::function<ne::Task<http::HttpResult<http::Response>>(const http::Request&)>;

	/**
	 * @class ServerConnection
	 * @brief 이미 수립된 전송(h2c 평문 또는 ALPN "h2" TLS) 하나에서 HTTP/2 요청을 처리하는 서버 연결입니다.
	 *
	 * 클라이언트 preface 를 확인하고 SETTINGS 를 교환한 뒤, 프레임을 읽어 각 스트림의 요청을 조립합니다.
	 * 스트림이 END_STREAM 으로 완결되면 핸들러를 호출하고 응답을 그 스트림으로 돌려보냅니다.
	 */
	class ServerConnection final :public Connection
	{
	public:
		ServerConnection(std::unique_ptr<ne::network::IStream> _stream, ne::io::Context& _context, Http2Handler _handler, const http::Limits _limits = {}) noexcept
			: Connection(std::move(_stream), _context)
			, handler(std::move(_handler))
			, limits(_limits) {}
		~ServerConnection() override = default;

	private:
		struct Stream
		{
			http::Method method{ http::Method::GET };
			string_t path;
			string_t authority;
			http::Headers headers;
			std::vector<byte_t> body;
			std::vector<byte_t> headerBlock;
			bool_t headersDone{ false };
			bool_t endStream{ false };
			bool_t rejected{ false }; // 크기 초과로 RST 됨 — 남은 프레임은 무시
		};

		Http2Handler handler;
		http::Limits limits;
		std::unordered_map<std::uint32_t, std::unique_ptr<Stream>> streams;
		bool_t goaway{ false };

	public:
		/** @brief 이 연결을 완결까지 처리합니다(요청/응답 반복). 피어가 닫거나 stop 되면 종료. */
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> Run(std::stop_token _stopToken);

	private:
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> DispatchStream(std::uint32_t _streamId, std::stop_token _stopToken);
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> SendResponse(std::uint32_t _streamId, http::Response _response, std::stop_token _stopToken);
		// 스트리밍 본문을 DATA 프레임으로 당겨 보낸다. 생산자 실패 시 해당 스트림만 RST_STREAM(연결 유지).
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> SendStreamingBody(std::uint32_t _streamId, const http::BodyProducer& _producer, std::stop_token _stopToken);
	};
}
