//
// Created by nebula on 24. 6. 21.
//

#ifndef SCHANNELBASE_H
#define SCHANNELBASE_H

#include "NebulaHandle.h"
#include "SspiWrapper.h"

BEGIN_NS(ne::protocol)
	using SecurityContextHandle = ne::Handle<CtxtHandle, decltype([](auto& _handle)
	{
		SspiWrapper::GetInstance().functions->DeleteSecurityContext(&_handle);
	})>;

	using CredentialsHandle = ne::Handle<CredHandle, decltype([](auto& _handle)
	{
		SspiWrapper::GetInstance().functions->FreeCredentialHandle(&_handle);
	})>;

	using HandshakeBuffer = ne::Handle<SecBuffer, decltype([](const auto& _buffer)
	{
		if (_buffer.pvBuffer) SspiWrapper::GetInstance().functions->FreeContextBuffer(_buffer.pvBuffer);
	})>;

	struct [[nodiscard]] HandshakeResult
	{
		SECURITY_STATUS status;
		HandshakeBuffer buffer;
	};


	/* Function */
	inline SecBufferDesc CreateBufferDescription(const std::span<SecBuffer> _buffers)
	{
		return { .ulVersion = SECBUFFER_VERSION, .cBuffers = static_cast<ulong_t>(_buffers.size()), .pBuffers = _buffers.data() };
	}

END_NS



[[nodiscard]]
constexpr ne::bool_t operator==(const CredHandle & _first, const CredHandle& _second) noexcept
{
	return _first.dwLower == _second.dwLower && _first.dwUpper == _second.dwUpper;
}
[[nodiscard]]
constexpr ne::bool_t operator!=(const CredHandle& _first, const CredHandle& _second) noexcept
{
	return !(_first == _second);
}

[[nodiscard]]
constexpr ne::bool_t operator==(const SecBuffer& _first, const SecBuffer& _second) noexcept
{
	return _first.pvBuffer == _second.pvBuffer;
}
[[nodiscard]]
constexpr ne::bool_t operator!=(const SecBuffer& _first, const SecBuffer& _second) noexcept
{
	return !(_first == _second);
}

#endif //SCHANNELBASE_H
