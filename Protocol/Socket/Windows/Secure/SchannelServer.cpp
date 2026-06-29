//
// Created by nebula on 24. 6. 21.
//

#include "SchannelServer.h"

#include <fstream>



BEGIN_NS(ne::protocol)
	SchannelServer::SchannelServer(TcpSocket* const _socket, PCCERT_CONTEXT _certContext)
		: credentialsHandle(AcquireCredentialsHandle(_certContext))
		, tlsBuffer(TlsMessageBuffer::Allocate())
		, socket(_socket)
	{
	}



	CredentialsHandle SchannelServer::AcquireCredentialsHandle(PCCERT_CONTEXT _certContext)
	{
		auto credentialsData = SCHANNEL_CRED
		{
			.dwVersion = SCHANNEL_CRED_VERSION,
			.cCreds = 1,
			.paCred = &_certContext,
			.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER,
			.dwFlags = SCH_USE_STRONG_CRYPTO
		};

		CredHandle handle;
		TimeStamp timeLimit;

		const auto result = SspiWrapper::GetInstance().functions->AcquireCredentialsHandleW(
			nullptr,
			const_cast<wchar_t*>(UNISP_NAME_W),
			SECPKG_CRED_INBOUND,
			nullptr,
			&credentialsData,
			nullptr,
			nullptr,
			&handle,
			&timeLimit
		);
		if (result != SEC_E_OK) throw ne::Exception("[SchannelServer/AcquireCredentialsHandle]", std::format("Failed to AcquireCredentialsHandleW function (result: {})", result));

		return CredentialsHandle(handle);
	}



	void_t SchannelServer::Handshake()
	{
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
				if (buffer->cbBuffer) Send(buffer);
				offset = 0;

				return;
			}
			else
			{
				throw ne::Exception("[SchannelServer/Handshake]", std::format("Schannel TLS handshake failed (result: {})", result));
			}
		}
	}

	HandshakeResult SchannelServer::HandshakeData(const std::span<std::byte> _buffer)
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
		unsigned long responseFlags = 0;

		TimeStamp timeLimit;

		const auto result = SspiWrapper::GetInstance().functions->AcceptSecurityContext(
			&credentialsHandle.Get(),
			securityContextHandle ? &securityContextHandle : nullptr,
			inputBuffers.empty() ? nullptr : &inputBufferDescription,
			requestFlags,
			0,
			securityContextHandle ? nullptr : &securityContextHandle,
			outputBuffers.empty() ? nullptr : &outputBufferDescription,
			&responseFlags,
			&timeLimit
		);

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

	void_t SchannelServer::Send(const HandshakeBuffer& _buffer) const
	{
		socket->Write(std::span{ static_cast<const std::byte*>(_buffer->pvBuffer), static_cast<std::size_t>(_buffer->cbBuffer) });
	}

	std::span<std::byte> SchannelServer::Receive(const std::size_t _offset)
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

		if (const auto result = socket->Read(buffer.subspan(_offset)); result < 0)
		{
			throw ne::Exception("[SchannelServer/Receive]", "The connection closed unexpectedly while reading handshake data");
		}
		else
		{
			return buffer.subspan(0, _offset + static_cast<std::size_t>(result));
		}
	}
END_NS
