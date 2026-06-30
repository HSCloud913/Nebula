//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstdint>
#include <cstddef>
#include "Type.h"

BEGIN_NS(ne::network::http_2)

	inline constexpr std::string_view kClientPreface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
	inline constexpr std::size_t      kFrameHeaderSize = 9;
	inline constexpr uint32_t         kDefaultWindowSize  = 65535;
	inline constexpr uint32_t         kDefaultMaxFrameSize = 16384;

	enum class FrameType : uint8_t
	{
		Data         = 0x0,
		Headers      = 0x1,
		Priority     = 0x2,
		RstStream    = 0x3,
		Settings     = 0x4,
		PushPromise  = 0x5,
		Ping         = 0x6,
		GoAway       = 0x7,
		WindowUpdate = 0x8,
		Continuation = 0x9,
	};

	namespace Flag
	{
		inline constexpr uint8_t EndStream  = 0x1;
		inline constexpr uint8_t Ack        = 0x1;
		inline constexpr uint8_t EndHeaders = 0x4;
		inline constexpr uint8_t Padded     = 0x8;
		inline constexpr uint8_t Priority   = 0x20;
	}

	namespace SettingId
	{
		inline constexpr uint16_t HeaderTableSize      = 0x1;
		inline constexpr uint16_t EnablePush           = 0x2;
		inline constexpr uint16_t MaxConcurrentStreams = 0x3;
		inline constexpr uint16_t InitialWindowSize    = 0x4;
		inline constexpr uint16_t MaxFrameSize         = 0x5;
		inline constexpr uint16_t MaxHeaderListSize    = 0x6;
	}

	namespace ErrorCode
	{
		inline constexpr uint32_t NoError       = 0x0;
		inline constexpr uint32_t ProtocolError = 0x1;
		inline constexpr uint32_t InternalError = 0x2;
		inline constexpr uint32_t Cancel        = 0x8;
	}

	[[nodiscard]] inline uint32_t ReadU24(const ne::byte_t* _p) noexcept
	{
		return (uint32_t(_p[0]) << 16) | (uint32_t(_p[1]) << 8) | uint32_t(_p[2]);
	}
	[[nodiscard]] inline uint32_t ReadU32(const ne::byte_t* _p) noexcept
	{
		return (uint32_t(_p[0]) << 24) | (uint32_t(_p[1]) << 16)
		     | (uint32_t(_p[2]) <<  8) |  uint32_t(_p[3]);
	}
	[[nodiscard]] inline uint16_t ReadU16(const ne::byte_t* _p) noexcept
	{
		return uint16_t((uint16_t(_p[0]) << 8) | uint16_t(_p[1]));
	}
	inline void WriteU24(ne::byte_t* _p, uint32_t _v) noexcept
	{
		_p[0] = ne::byte_t((_v >> 16) & 0xFF);
		_p[1] = ne::byte_t((_v >>  8) & 0xFF);
		_p[2] = ne::byte_t( _v        & 0xFF);
	}
	inline void WriteU32(ne::byte_t* _p, uint32_t _v) noexcept
	{
		_p[0] = ne::byte_t((_v >> 24) & 0xFF);
		_p[1] = ne::byte_t((_v >> 16) & 0xFF);
		_p[2] = ne::byte_t((_v >>  8) & 0xFF);
		_p[3] = ne::byte_t( _v        & 0xFF);
	}
	inline void WriteU16(ne::byte_t* _p, uint16_t _v) noexcept
	{
		_p[0] = ne::byte_t((_v >> 8) & 0xFF);
		_p[1] = ne::byte_t( _v       & 0xFF);
	}

	inline void BuildFrameHeader(ne::byte_t* _buf,
	                             uint32_t    _length,
	                             FrameType   _type,
	                             uint8_t     _flags,
	                             uint32_t    _streamId) noexcept
	{
		WriteU24(_buf, _length);
		_buf[3] = ne::byte_t(_type);
		_buf[4] = _flags;
		WriteU32(_buf + 5, _streamId & 0x7FFFFFFFu);
	}

	struct FrameHeader
	{
		uint32_t  length;
		FrameType type;
		uint8_t   flags;
		uint32_t  streamId;

		[[nodiscard]] static FrameHeader Parse(const ne::byte_t* _buf) noexcept
		{
			return {
				ReadU24(_buf),
				FrameType(_buf[3]),
				_buf[4],
				ReadU32(_buf + 5) & 0x7FFFFFFFu
			};
		}
	};

END_NS
