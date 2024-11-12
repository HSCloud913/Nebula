//
// Created by hsclo on 24. 5. 29.
//

#ifndef TCPSOCKET_H
#define TCPSOCKET_H

#include <memory>
#include "Type.h"
#include "SocketBase.h"
#if defined(_WIN32)
#	include <ws2tcpip.h>
#	include "Windows/Base/WindowsSocketBase.h"
#elif defined(IS_POSIX)
#	include <netdb.h>
#endif

BEGIN_NS(ne::protocol)
	class TcpSocket final
	{
		NEBULA_NON_COPYABLE_MOVABLE(TcpSocket);

	public:
		explicit TcpSocket(string_view_t _server, int_t _port) noexcept;
		explicit TcpSocket(socket_t _socket) noexcept;

	private:
#if defined(_WIN32)
		using AddressInfo = std::unique_ptr<addrinfoW, decltype([](auto _p)
		{
			if (_p != nullptr) FreeAddrInfoW(_p);
		})>;
#elif defined(IS_POSIX)
		using AddressInfo = std::unique_ptr<addrinfo, decltype([](auto _p)
		{
			if (_p != nullptr) freeaddrinfo(_p);
		})>;
#endif

	private:
#if defined(_WIN32)
		WindowsSocketBase windowsSocketBase;
#endif
		SocketHandle handle;
		AddressInfo addressInfo;
		bool_t isNonblocking = false;

	public:
		void_t Connect();
		void_t Reconnect();
		[[nodiscard]] bool_t IsConnected() const;
		void_t Bind();
		void_t Listen();
		[[nodiscard]] socket_t Accept(sockaddr* _sockAddr = nullptr, socklen_t* _addrLength = nullptr);

		void_t Select();
		void_t Poll();
#if defined(__linux)
		void_t Epoll();
#elif defined(__APPLE__)
		void_t Kqueue();
#endif

	public:
		void_t SetSocketOption(int _option, const char* _value, int _valueLength) const;
		void_t SocketControl(long_t _option, ulong_t _value, bool _isEnable = true) const;
		void_t SetNonBlockingMode(bool_t _isNonblocking);

	public:
		[[nodiscard]] std::size_t Read(const std::span<std::byte> _buffer);
		void_t Write(const std::span<const std::byte> _data);

	private:
		[[nodiscard]] SocketHandle CreateHandle() const;
		[[nodiscard]] static AddressInfo GetAddressInfo(const string_view_t _server, const int_t _port);

	public:
		[[nodiscard]] socket_t GetHandle() const;
	};

END_NS

typedef ne::protocol::TcpSocket NebulaTcpSocket;

#endif //TCPSOCKET_H
