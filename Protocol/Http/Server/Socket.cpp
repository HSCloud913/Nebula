//
// Created by nebula on 24. 6. 12.
//

#include "Socket.h"

#include "Socket/TcpSocket.h"
#include "Socket/TlsSocket.h"



BEGIN_NS(ne::protocol::Http::Server)
	class Socket::Impl final
	{
	public:
		Impl(const string_view_t _server, const int_t _port, const bool_t _isTlsEncrypted)
			: socket(SelectSocket(_server, _port, _isTlsEncrypted))
		{
		}

	private:
		using SocketVariant = std::variant<TcpSocket, NebulaTlsSocket>;
		SocketVariant socket;

	private:
		[[nodiscard]] static SocketVariant SelectSocket(const string_view_t _server, const int_t _port, const bool_t _isTlsEncrypted)
		{
			if (_isTlsEncrypted) return NebulaTlsSocket(_server, _port);

			return TcpSocket(_server, _port);
		}

	public:
		void_t Listen()
		{
			if (std::holds_alternative<TcpSocket>(socket))
			{
				std::get<TcpSocket>(socket).Bind();
				std::get<TcpSocket>(socket).Listen();
			}
			else if (std::holds_alternative<NebulaTlsSocket>(socket))
			{
				std::get<NebulaTlsSocket>(socket).Bind();
				std::get<NebulaTlsSocket>(socket).Listen();
			}
		}

		[[nodiscard]] socket_t Accept()
		{
			if (std::holds_alternative<TcpSocket>(socket))
			{
				return std::get<TcpSocket>(socket).Accept();
			}

			return std::get<NebulaTlsSocket>(socket).Accept();
		}

#if defined(IS_POSIX)
		void_t Init()
		{
			if (std::holds_alternative<NebulaTlsSocket>(socket))
			{
				std::get<NebulaTlsSocket>(socket).Init();
			}
		}

		void_t LoadCertificates(string_view_t _crt, string_view_t _key)
		{
			if (std::holds_alternative<NebulaTlsSocket>(socket))
			{
				return std::get<NebulaTlsSocket>(socket).LoadCertificates(_crt, _key);
			}
		}

		SSL_CTX* GetTlsContext() const
		{
			if (std::holds_alternative<NebulaTlsSocket>(socket))
			{
				return std::get<NebulaTlsSocket>(socket).GetTlsContext();
			}

			return nullptr;
		}
#endif
	};



	/*--------------------------------------------------*/



	Socket::Socket(const string_view_t _server, const int_t _port, const bool_t _isTlsEncrypted)
		: impl(std::make_unique<Impl>(_server, _port, _isTlsEncrypted))
	{
	}
	Socket::~Socket() = default;

	Socket::Socket(Socket&&) noexcept = default;
	Socket& Socket::operator=(Socket&&) noexcept = default;



	void_t Socket::Listen() const
	{
		impl->Listen();
	}

	socket_t Socket::Accept() const
	{
		return impl->Accept();
	}

#if defined(IS_POSIX)
	void_t Socket::Init() const
	{
		impl->Init();
	}

	void_t Socket::LoadCertificates(string_view_t _crt, string_view_t _key) const
	{
		impl->LoadCertificates(_crt, _key);
	}

	SSL_CTX* Socket::GetTlsContext() const
	{
		return impl->GetTlsContext();
	}
#endif

END_NS
