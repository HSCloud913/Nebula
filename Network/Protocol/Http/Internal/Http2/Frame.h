//
// Created by hscloud on 26. 7. 28.
//

#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include "Base/Type.h"

namespace ne::network::http_2::internal
{
	// HTTP/2 연결 preface(클라이언트가 연결 직후 보내는 24바이트 매직). RFC 9113 §3.4.
	inline constexpr string_view_t ConnectionPreface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

	// 프레임 고정 헤더 크기(길이24 + 타입8 + 플래그8 + R1+streamId31). RFC 9113 §4.1.
	inline constexpr std::size_t FrameHeaderSize = 9;

	// SETTINGS 로 협상 전 기본 최대 프레임 페이로드 크기(16KiB). RFC 9113 §6.5.2.
	inline constexpr std::uint32_t DefaultMaxFrameSize = 16384;

	// 스트림/연결 기본 흐름제어 윈도우(65535). RFC 9113 §6.9.2.
	inline constexpr std::int32_t DefaultInitialWindowSize = 65535;

	/** @brief 프레임 타입. RFC 9113 §6. */
	enum class FrameType : byte_t
	{
		DATA          = 0x0,
		HEADERS       = 0x1,
		PRIORITY      = 0x2,
		RST_STREAM    = 0x3,
		SETTINGS      = 0x4,
		PUSH_PROMISE  = 0x5,
		PING          = 0x6,
		GOAWAY        = 0x7,
		WINDOW_UPDATE = 0x8,
		CONTINUATION  = 0x9,
	};

	/** @brief 프레임 플래그 비트(타입별로 의미가 다름). RFC 9113 §6. */
	enum FrameFlag : byte_t
	{
		FLAG_NONE        = 0x0,
		FLAG_END_STREAM  = 0x1, // DATA / HEADERS
		FLAG_ACK         = 0x1, // SETTINGS / PING
		FLAG_END_HEADERS = 0x4, // HEADERS / CONTINUATION / PUSH_PROMISE
		FLAG_PADDED      = 0x8, // DATA / HEADERS / PUSH_PROMISE
		FLAG_PRIORITY    = 0x20, // HEADERS
	};

	/** @brief SETTINGS 파라미터 식별자. RFC 9113 §6.5.2. */
	enum class SettingsId : std::uint16_t
	{
		HEADER_TABLE_SIZE      = 0x1,
		ENABLE_PUSH            = 0x2,
		MAX_CONCURRENT_STREAMS = 0x3,
		INITIAL_WINDOW_SIZE    = 0x4,
		MAX_FRAME_SIZE         = 0x5,
		MAX_HEADER_LIST_SIZE   = 0x6,
	};

	/** @brief RST_STREAM / GOAWAY 에 실리는 에러 코드. RFC 9113 §7. @note NO_ERROR 는 winerror.h 매크로와 충돌하므로 NO_ERROR_ 로 표기. */
	enum class ErrorCode : std::uint32_t
	{
		NO_ERROR_           = 0x0,
		PROTOCOL_ERROR      = 0x1,
		INTERNAL_ERROR      = 0x2,
		FLOW_CONTROL_ERROR  = 0x3,
		SETTINGS_TIMEOUT    = 0x4,
		STREAM_CLOSED       = 0x5,
		FRAME_SIZE_ERROR    = 0x6,
		REFUSED_STREAM      = 0x7,
		CANCEL              = 0x8,
		COMPRESSION_ERROR   = 0x9,
		CONNECT_ERROR       = 0xa,
		ENHANCE_YOUR_CALM   = 0xb,
		INADEQUATE_SECURITY = 0xc,
		HTTP_1_1_REQUIRED   = 0xd,
	};

	/**
	 * @class FrameHeader
	 * @brief 파싱된 프레임 고정 헤더입니다. payload 자체는 담지 않고 길이/타입/플래그/스트림ID만 나타냅니다.
	 */
	struct FrameHeader
	{
		std::uint32_t length{ 0 };  // 페이로드 바이트 수(24비트)
		FrameType     type{ FrameType::DATA };
		byte_t        flags{ 0 };
		std::uint32_t streamId{ 0 }; // 상위 R 비트를 제외한 31비트

		[[nodiscard]] bool_t HasFlag(const FrameFlag _flag) const noexcept { return (flags & _flag) != 0; }
	};

	/** @brief 하나의 SETTINGS 파라미터(식별자 + 값). */
	struct SettingsEntry
	{
		SettingsId    id;
		std::uint32_t value;
	};

	// ── 정수 (big-endian) 읽기/쓰기 ──
	void_t WriteUint16(std::vector<byte_t>& _out, std::uint16_t _value);
	void_t WriteUint24(std::vector<byte_t>& _out, std::uint32_t _value);
	void_t WriteUint32(std::vector<byte_t>& _out, std::uint32_t _value);

	[[nodiscard]] std::uint16_t ReadUint16(std::span<const byte_t> _data) noexcept; // 앞 2바이트
	[[nodiscard]] std::uint32_t ReadUint24(std::span<const byte_t> _data) noexcept; // 앞 3바이트
	[[nodiscard]] std::uint32_t ReadUint32(std::span<const byte_t> _data) noexcept; // 앞 4바이트

	/** @brief 9바이트 프레임 헤더를 파싱합니다. 입력이 9바이트 미만이면 nullopt. */
	[[nodiscard]] std::optional<FrameHeader> ParseFrameHeader(std::span<const byte_t> _data) noexcept;

	// ── 프레임 직렬화(헤더+페이로드를 _out 뒤에 append) ──

	/** @brief 임의 타입 프레임 하나를 append 합니다(페이로드는 이미 준비된 바이트열). */
	void_t AppendFrame(std::vector<byte_t>& _out, FrameType _type, byte_t _flags, std::uint32_t _streamId, std::span<const byte_t> _payload);

	/** @brief DATA 프레임 하나(단일 조각, 이미 maxFrameSize 이하로 잘린 상태). */
	void_t AppendData(std::vector<byte_t>& _out, std::uint32_t _streamId, std::span<const byte_t> _data, bool_t _endStream);

	/**
	 * @brief HPACK 헤더 블록을 HEADERS(+필요 시 CONTINUATION)로 잘라 append 합니다.
	 * @note _maxFrameSize 를 넘으면 CONTINUATION 으로 분할하며, 마지막 조각에만 END_HEADERS 를 세웁니다.
	 */
	void_t AppendHeaderBlock(std::vector<byte_t>& _out, std::uint32_t _streamId, std::span<const byte_t> _headerBlock, bool_t _endStream, std::uint32_t _maxFrameSize);

	void_t AppendSettings(std::vector<byte_t>& _out, std::span<const SettingsEntry> _entries);
	void_t AppendSettingsAck(std::vector<byte_t>& _out);
	void_t AppendWindowUpdate(std::vector<byte_t>& _out, std::uint32_t _streamId, std::uint32_t _increment);
	void_t AppendRstStream(std::vector<byte_t>& _out, std::uint32_t _streamId, ErrorCode _code);
	void_t AppendPing(std::vector<byte_t>& _out, std::span<const byte_t> _opaque8, bool_t _ack);
	void_t AppendGoAway(std::vector<byte_t>& _out, std::uint32_t _lastStreamId, ErrorCode _code, std::span<const byte_t> _debug = {});
}
