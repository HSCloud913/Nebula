//
// Created by hscloud on 26. 7. 28.
//

#include "Network/Protocol/Http/Internal/Http2/Frame.h"

#include <algorithm>

namespace ne::network::http_2::internal
{
	void_t WriteUint16(std::vector<byte_t>& _out, const std::uint16_t _value)
	{
		_out.push_back(static_cast<byte_t>((_value >> 8) & 0xFF));
		_out.push_back(static_cast<byte_t>(_value & 0xFF));
	}

	void_t WriteUint24(std::vector<byte_t>& _out, const std::uint32_t _value)
	{
		_out.push_back(static_cast<byte_t>((_value >> 16) & 0xFF));
		_out.push_back(static_cast<byte_t>((_value >> 8) & 0xFF));
		_out.push_back(static_cast<byte_t>(_value & 0xFF));
	}

	void_t WriteUint32(std::vector<byte_t>& _out, const std::uint32_t _value)
	{
		_out.push_back(static_cast<byte_t>((_value >> 24) & 0xFF));
		_out.push_back(static_cast<byte_t>((_value >> 16) & 0xFF));
		_out.push_back(static_cast<byte_t>((_value >> 8) & 0xFF));
		_out.push_back(static_cast<byte_t>(_value & 0xFF));
	}

	std::uint16_t ReadUint16(const std::span<const byte_t> _data) noexcept
	{
		return static_cast<std::uint16_t>((static_cast<std::uint16_t>(_data[0]) << 8) | static_cast<std::uint16_t>(_data[1]));
	}

	std::uint32_t ReadUint24(const std::span<const byte_t> _data) noexcept
	{
		return (static_cast<std::uint32_t>(_data[0]) << 16) | (static_cast<std::uint32_t>(_data[1]) << 8) | static_cast<std::uint32_t>(_data[2]);
	}

	std::uint32_t ReadUint32(const std::span<const byte_t> _data) noexcept
	{
		return (static_cast<std::uint32_t>(_data[0]) << 24) | (static_cast<std::uint32_t>(_data[1]) << 16) | (static_cast<std::uint32_t>(_data[2]) << 8) | static_cast<std::uint32_t>(_data[3]);
	}

	std::optional<FrameHeader> ParseFrameHeader(const std::span<const byte_t> _data) noexcept
	{
		if (_data.size() < FrameHeaderSize) return std::nullopt;

		FrameHeader header;
		header.length = ReadUint24(_data.subspan(0, 3));
		header.type = static_cast<FrameType>(_data[3]);
		header.flags = _data[4];
		header.streamId = ReadUint32(_data.subspan(5, 4)) & 0x7FFFFFFFu; // 상위 R 비트 제거

		return header;
	}

	void_t AppendFrame(std::vector<byte_t>& _out, const FrameType _type, const byte_t _flags, const std::uint32_t _streamId, const std::span<const byte_t> _payload)
	{
		WriteUint24(_out, static_cast<std::uint32_t>(_payload.size()));
		_out.push_back(static_cast<byte_t>(_type));
		_out.push_back(_flags);
		WriteUint32(_out, _streamId & 0x7FFFFFFFu);
		_out.insert(_out.end(), _payload.begin(), _payload.end());
	}

	void_t AppendData(std::vector<byte_t>& _out, const std::uint32_t _streamId, const std::span<const byte_t> _data, const bool_t _endStream)
	{
		AppendFrame(_out, FrameType::DATA, _endStream ? FLAG_END_STREAM : FLAG_NONE, _streamId, _data);
	}

	void_t AppendHeaderBlock(std::vector<byte_t>& _out, const std::uint32_t _streamId, const std::span<const byte_t> _headerBlock, const bool_t _endStream, const std::uint32_t _maxFrameSize)
	{
		const std::uint32_t maxChunk = _maxFrameSize == 0 ? DefaultMaxFrameSize : _maxFrameSize;

		std::size_t offset = 0;
		bool_t first = true;
		do
		{
			const std::size_t remaining = _headerBlock.size() - offset;
			const std::size_t chunk = std::min<std::size_t>(remaining, maxChunk);
			const bool_t last = (offset + chunk) >= _headerBlock.size();

			byte_t flags = FLAG_NONE;
			if (last) flags |= FLAG_END_HEADERS;
			if (first && _endStream) flags |= FLAG_END_STREAM;

			AppendFrame(_out, first ? FrameType::HEADERS : FrameType::CONTINUATION, flags, _streamId, _headerBlock.subspan(offset, chunk));

			offset += chunk;
			first = false;
		}
		while (offset < _headerBlock.size());
	}

	void_t AppendSettings(std::vector<byte_t>& _out, const std::span<const SettingsEntry> _entries)
	{
		std::vector<byte_t> payload;
		payload.reserve(_entries.size() * 6);
		for (const auto& entry : _entries)
		{
			WriteUint16(payload, static_cast<std::uint16_t>(entry.id));
			WriteUint32(payload, entry.value);
		}

		AppendFrame(_out, FrameType::SETTINGS, FLAG_NONE, 0, payload);
	}

	void_t AppendSettingsAck(std::vector<byte_t>& _out)
	{
		AppendFrame(_out, FrameType::SETTINGS, FLAG_ACK, 0, {});
	}

	void_t AppendWindowUpdate(std::vector<byte_t>& _out, const std::uint32_t _streamId, const std::uint32_t _increment)
	{
		std::vector<byte_t> payload;
		WriteUint32(payload, _increment & 0x7FFFFFFFu);
		AppendFrame(_out, FrameType::WINDOW_UPDATE, FLAG_NONE, _streamId, payload);
	}

	void_t AppendRstStream(std::vector<byte_t>& _out, const std::uint32_t _streamId, const ErrorCode _code)
	{
		std::vector<byte_t> payload;
		WriteUint32(payload, static_cast<std::uint32_t>(_code));
		AppendFrame(_out, FrameType::RST_STREAM, FLAG_NONE, _streamId, payload);
	}

	void_t AppendPing(std::vector<byte_t>& _out, const std::span<const byte_t> _opaque8, const bool_t _ack)
	{
		byte_t opaque[8] = {};
		for (std::size_t i = 0; i < 8 && i < _opaque8.size(); ++i) opaque[i] = _opaque8[i];
		AppendFrame(_out, FrameType::PING, _ack ? FLAG_ACK : FLAG_NONE, 0, std::span<const byte_t>(opaque, 8));
	}

	void_t AppendGoAway(std::vector<byte_t>& _out, const std::uint32_t _lastStreamId, const ErrorCode _code, const std::span<const byte_t> _debug)
	{
		std::vector<byte_t> payload;
		WriteUint32(payload, _lastStreamId & 0x7FFFFFFFu);
		WriteUint32(payload, static_cast<std::uint32_t>(_code));
		payload.insert(payload.end(), _debug.begin(), _debug.end());
		AppendFrame(_out, FrameType::GOAWAY, FLAG_NONE, 0, payload);
	}
}
