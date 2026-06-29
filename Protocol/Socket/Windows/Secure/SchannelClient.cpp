//
// Created by nebula on 24. 5. 29.
//

#include "SchannelClient.h"

#include <array>
#include "StringFormat.h"
#include "Http/HttpUtil.h"



BEGIN_NS(ne::protocol)
	SchannelClient::SchannelClient(TcpSocket* _socket, const string_view_t _server)
		: credentialsHandle(AcquireCredentialsHandle())
		, tlsBuffer(TlsMessageBuffer::Allocate())
		, socket(_socket)
		, server(StringFormat::UTF8toWCS(string_t(_server).c_str()))
	{
	}



	CredentialsHandle SchannelClient::AcquireCredentialsHandle()
	{
		auto credentialsData = SCHANNEL_CRED
		{
			.dwVersion = SCHANNEL_CRED_VERSION,
			.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT,
			.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO
		};

		CredHandle handle;
		TimeStamp timeLimit;

		const auto result = SspiWrapper::GetInstance().functions->AcquireCredentialsHandleW(
			nullptr,
			const_cast<wchar_t*>(UNISP_NAME_W),
			SECPKG_CRED_OUTBOUND,
			nullptr,
			&credentialsData,
			nullptr,
			nullptr,
			&handle,
			&timeLimit
		);
		if (result != SEC_E_OK) throw ne::Exception("[SchannelClient/AcquireCredentialsHandle]", std::format("Failed to AcquireCredentialsHandleW function (result: {})", result));

		return CredentialsHandle(handle);
	}



	void_t SchannelClient::Handshake()
	{
		if (const auto [result, buffer] = HandshakeData({}); result != SEC_I_CONTINUE_NEEDED)
		{
			throw ne::Exception("[SchannelClient/Handshake]", std::format("Schannel TLS handshake initialization failed (result: {})", result));
		}
		else
		{
			Send(buffer);
		}

		auto offset = std::size_t{};
		while (true)
		{
			const auto data = Receive(offset);
			if (const auto [result, buffer] = HandshakeData(data); result == SEC_I_CONTINUE_NEEDED)
			{
				if (buffer->cbBuffer) Send(buffer);
				offset = 0;
			}
			else if (result == SEC_E_INCOMPLETE_MESSAGE)
			{
				offset = data.size();
			}
			else if (result == SEC_E_OK)
			{
				return;
			}
			else
			{
				throw ne::Exception("[SchannelClient/Handshake]", std::format("Schannel TLS handshake failed (result: {})", result));
			}
		}
	}

	HandshakeResult SchannelClient::HandshakeData(const std::span<std::byte> _buffer)
	{
		auto inputBuffers = std::array
		{
			SecBuffer
			{
				.cbBuffer = static_cast<unsigned long>(_buffer.size()),
				.BufferType = SECBUFFER_TOKEN,
				.pvBuffer = _buffer.data(),
			},
			SecBuffer{}
		};
		auto inputBufferDescription = CreateBufferDescription(inputBuffers);

		auto outputBuffers = std::array
		{
			SecBuffer{ .BufferType = SECBUFFER_TOKEN },
			SecBuffer{ .BufferType = SECBUFFER_ALERT },
			SecBuffer{}
		};
		auto outputBufferDescription = CreateBufferDescription(outputBuffers);

		constexpr auto requestFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
		unsigned long responseFlags;

		const auto result = SspiWrapper::GetInstance().functions->InitializeSecurityContextW(
			&credentialsHandle.Get(),
			securityContextHandle ? &securityContextHandle : nullptr,
			server.data(),
			requestFlags,
			0,
			0,
			inputBuffers.empty() ? nullptr : &inputBufferDescription,
			0,
			&securityContextHandle,
			&outputBufferDescription,
			&responseFlags,
			nullptr
		);

		if (requestFlags != responseFlags) throw ne::Exception("[SchannelClient/HandshakeData]", "The schannel security context flags were not supported");

		return HandshakeResult{ [&]
								{
									if (inputBuffers[1].BufferType == SECBUFFER_EXTRA)
									{
										tlsBuffer.data = _buffer.last(inputBuffers[1].cbBuffer);
									}

									if (result == SEC_I_COMPLETE_AND_CONTINUE || result == SEC_I_COMPLETE_NEEDED)
									{
										SspiWrapper::GetInstance().functions->CompleteAuthToken(&securityContextHandle, &outputBufferDescription);

										return (result == SEC_I_COMPLETE_AND_CONTINUE) ? SEC_I_CONTINUE_NEEDED : SEC_E_OK;
									}

									return result;
								}(),
								HandshakeBuffer{ outputBuffers[0] } };
	}

	void_t SchannelClient::Send(const HandshakeBuffer& _buffer) const
	{
		socket->Write(std::span{ static_cast<const std::byte*>(_buffer->pvBuffer), static_cast<std::size_t>(_buffer->cbBuffer) });
	}

	std::span<std::byte> SchannelClient::Receive(const std::size_t _offset)
	{
		const auto buffer = tlsBuffer.GetBuffer();

		if (!tlsBuffer.data.empty())
		{
			assert(_offset == 0);

			const auto dataSize = tlsBuffer.data.size();
			std::ranges::copy_backward(tlsBuffer.data, buffer.begin() + dataSize);
			tlsBuffer.data = {};

			return buffer.first(dataSize);
		}

		if (const auto result = socket->Read(buffer.subspan(_offset)); result <= 0)
		{
			throw ne::Exception("[SchannelClient/Receive]", "The connection closed unexpectedly while reading handshake data");
		}
		else
		{
			return buffer.subspan(0, _offset + result);
		}
	}

END_NS
