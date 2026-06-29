//
// Created by nebula on 24. 5. 29.
//

#ifndef SCHANNELCLIENT_H
#define SCHANNELCLIENT_H

#include <span>
#include "Socket/TcpSocket.h"
#include "SchannelBase.h"
#include <schannel.h>
#include "TlsMessageBuffer.h"

BEGIN_NS(ne::protocol)
	class SchannelClient final
	{
		NEBULA_NON_COPYABLE_MOVABLE(SchannelClient)

	public:
		[[nodiscard]] SchannelClient(TcpSocket* _socket, string_view_t _server);
		~SchannelClient() = default;

	private:
		CredentialsHandle credentialsHandle;
		SecurityContextHandle securityContextHandle;
		TlsMessageBuffer tlsBuffer;

		TcpSocket* socket;
		wstring_t server;

	private:
		[[nodiscard]] static CredentialsHandle AcquireCredentialsHandle();

	private:
		void_t Handshake();
		HandshakeResult HandshakeData(std::span<std::byte> _buffer);
		void_t Send(const HandshakeBuffer& _buffer) const;
		std::span<std::byte> Receive(std::size_t _offset = {});

	public:
		std::pair<SecurityContextHandle, TlsMessageBuffer> operator()() &&;
	};



	inline std::pair<SecurityContextHandle, TlsMessageBuffer> SchannelClient::operator()() &&
	{
		Handshake();
		return { std::move(securityContextHandle), std::move(tlsBuffer) };
	}

END_NS

#endif //SCHANNELCLIENT_H
