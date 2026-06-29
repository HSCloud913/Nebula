//
// Created by nebula on 24. 5. 29.
//

#include "Socket.h"

#include "Socket/TcpSocket.h"
#include "Socket/TlsSocket.h"



BEGIN_NS(ne::protocol::Http::Client)
	class Socket::Impl final
	{
	public:
		Impl(string_view_t _server, const int_t _port, const bool_t _isTlsEncrypted) : socket(SelectSocket(_server, _port, _isTlsEncrypted)) {}

	private:
		using SocketVariant = std::variant<NebulaTcpSocket, NebulaTlsSocket>;
		SocketVariant socket;

	private:
		[[nodiscard]] static SocketVariant SelectSocket(string_view_t _server, int_t _port, const bool_t _isTlsEncrypted)
		{
			if (_isTlsEncrypted) return NebulaTlsSocket(_server, _port);

			return NebulaTcpSocket(_server, _port);
		}

	public:
		void_t Connect()
		{
			if (std::holds_alternative<NebulaTcpSocket>(socket))
			{
				std::get<NebulaTcpSocket>(socket).Connect();
			}
			else if (std::holds_alternative<NebulaTlsSocket>(socket))
			{
				std::get<NebulaTlsSocket>(socket).Connect();
			}
		}

		void_t SetTimeout(const std::chrono::milliseconds _timeout)
		{
			if (std::holds_alternative<NebulaTcpSocket>(socket))
			{
				std::get<NebulaTcpSocket>(socket).SetTimeout(_timeout);
			}
			else
			{
				std::get<NebulaTlsSocket>(socket).SetTimeout(_timeout);
			}
		}

		[[nodiscard]] bool_t IsAlive()
		{
			if (std::holds_alternative<NebulaTcpSocket>(socket)) return std::get<NebulaTcpSocket>(socket).IsAlive();

			return std::get<NebulaTlsSocket>(socket).IsAlive();
		}

		[[nodiscard]] longlong_t Read(const std::span<std::byte> _buffer)
		{
			if (std::holds_alternative<NebulaTcpSocket>(socket)) return std::get<NebulaTcpSocket>(socket).Read(_buffer);

			return std::get<NebulaTlsSocket>(socket).Read(_buffer);
		}

		void_t Write(const std::span<const std::byte> _buffer)
		{
			if (std::holds_alternative<NebulaTcpSocket>(socket))
			{
				std::get<TcpSocket>(socket).Write(_buffer);
			}
			else
			{
				std::get<NebulaTlsSocket>(socket).Write(_buffer);
			}
		}
	};



	/*--------------------------------------------------*/



	Socket::Socket(string_view_t _server, const int_t _port, const bool_t _isTlsEncrypted) : impl(std::make_unique<Impl>(_server, _port, _isTlsEncrypted)) {}
	Socket::~Socket() = default;

	Socket::Socket(Socket&&) noexcept = default;
	Socket& Socket::operator=(Socket&&) noexcept = default;



	void_t Socket::Connect() const
	{
		impl->Connect();
	}

	void_t Socket::SetTimeout(const std::chrono::milliseconds _timeout) const
	{
		impl->SetTimeout(_timeout);
	}

	bool_t Socket::IsAlive() const
	{
		return impl->IsAlive();
	}

	longlong_t Socket::Read(std::span<std::byte> _buffer) const
	{
		return impl->Read(_buffer);
	}

	std::vector<std::byte> Socket::Read(const std::size_t _bytes) const
	{
		auto buffer = std::vector<std::byte>(_bytes);
		if (const auto readSize = Read(buffer); readSize > 0)
		{
			buffer.resize(readSize);
			return buffer;
		}

		return {};
	}

	void_t Socket::Write(const std::span<const std::byte> _data) const
	{
		impl->Write(_data);
	}

	void_t Socket::Write(string_view_t _stringView) const
	{
		Write(StringToData<std::byte>(_stringView));
	}

END_NS
