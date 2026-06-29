//
// Created by nebula on 24. 6. 21.
//

#ifndef SCHANNELSERVER_H
#define SCHANNELSERVER_H

#include <span>
#include "Socket/TcpSocket.h"
#include "SchannelBase.h"
#include <schannel.h>
#include "TlsMessageBuffer.h"

BEGIN_NS(ne::protocol)
	class SchannelServer final
	{
		NEBULA_NON_COPYABLE_MOVABLE(SchannelServer)

	public:
		[[nodiscard]] SchannelServer(TcpSocket* const _socket, PCCERT_CONTEXT _certContext);
		~SchannelServer() = default;

	private:
		CredentialsHandle credentialsHandle;
		SecurityContextHandle securityContextHandle;
		TlsMessageBuffer tlsBuffer;
		TcpSocket* socket;

	private:
		[[nodiscard]] static CredentialsHandle AcquireCredentialsHandle(PCCERT_CONTEXT _certContext);

	private:
		void_t Handshake();
		HandshakeResult HandshakeData(const std::span<std::byte> _buffer);
		void_t Send(const HandshakeBuffer& _buffer) const;
		std::span<std::byte> Receive(const std::size_t _offset = {});

	public:
		std::pair<SecurityContextHandle, TlsMessageBuffer> operator()() &&;
	};

	inline std::pair<SecurityContextHandle, TlsMessageBuffer> SchannelServer::operator()() &&
	{
		Handshake();
		return { std::move(securityContextHandle), std::move(tlsBuffer) };
	}

END_NS

#endif //SCHANNELSERVER_H
