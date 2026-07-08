//
// Created by csw on 26. 7. 7..
//

#pragma once

BEGIN_NS(ne::io)
	enum class IoCapability : uint32_t
	{
		None         = 0,
		Reactor      = 1u << 0, // Watch/Unwatch (epoll, IOCP reactor 경로)
		Proactor     = 1u << 1, // SubmitSend/SubmitReceive (IOCP, io_uring)
		RegisteredIo = 1u << 2, // RIO(Win) / registered buffers(io_uring: *_FIXED, SEND_ZC)
		FileTransmit = 1u << 3, // TransmitFile / sendfile / splice
	};

	[[nodiscard]] constexpr IoCapability operator|(const IoCapability _lhs, const IoCapability _rhs) noexcept
	{
		return static_cast<IoCapability>(static_cast<uint32_t>(_lhs) | static_cast<uint32_t>(_rhs));
	}

	[[nodiscard]] constexpr IoCapability operator&(const IoCapability _lhs, const IoCapability _rhs) noexcept
	{
		return static_cast<IoCapability>(static_cast<uint32_t>(_lhs) & static_cast<uint32_t>(_rhs));
	}

	// _set 에 _bit 가 모두 포함되는지.
	[[nodiscard]] constexpr bool_t HasCapability(const IoCapability _set, const IoCapability _bit) noexcept
	{
		return (static_cast<uint32_t>(_set) & static_cast<uint32_t>(_bit)) == static_cast<uint32_t>(_bit);
	}

END_NS
