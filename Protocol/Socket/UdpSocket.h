//
// Created by nebula on 24. 5. 29.
//

#ifndef UDPSOCKET_H
#define UDPSOCKET_H

#include <memory>
#include <functional>
#include <span>
#include <vector>

#include "Type.h"
#include "SocketBase.h"
#if defined(_WIN32)
#	include <ws2tcpip.h>
#	include "Windows/Base/WindowsSocketBase.h"
#elif defined(IS_POSIX)
#	include <netdb.h>
#endif

BEGIN_NS(ne::protocol)
	class UdpSocket final
	{
	public:
		explicit UdpSocket(socket_t _socket) noexcept : handle(_socket) {}
		UdpSocket(string_view_t _server, int_t _port);

	private:
#if defined(_WIN32)
		using AddressInfo = std::unique_ptr<addrinfoW, decltype([](auto _p) { if (_p != nullptr) FreeAddrInfoW(_p); })>;
#elif defined(IS_POSIX)
		using AddressInfo = std::unique_ptr<addrinfo, decltype([](auto _p)
		{
			if (_p != nullptr) freeaddrinfo(_p);
		})>;
#endif
		using ReadHandler = std::function<void(std::vector<std::byte>)>;
		using ExceptionHandler = std::function<void(const std::string&)>;

	private:
#if defined(_WIN32)
		WindowsSocketBase windowsSocketBase;
#endif
		AddressInfo addressInfo;
		SocketHandle handle;
		bool_t isNonblocking = false;
		bool_t isConnected = false;

	private:
		ReadHandler readHandler;
		ExceptionHandler exceptionHandler;

	public:
		void_t Bind();
		void_t Connect();
		[[nodiscard]] bool_t IsConnected() const noexcept { return isConnected; }

		void_t Select();
		void_t Poll();

	public:
		void_t SetSocketOption(int _option, const char* _value, int _valueLength) const;
		void_t SocketControl(long_t _option, ulong_t _value, bool _isEnable = true) const;

		void_t SetSocketMode(bool _isNonblocking);

	public:
		void_t RegisterReadHandler(const ReadHandler& _readHandler) noexcept { readHandler = _readHandler; }
		void_t RegisterExceptionHandler(const ExceptionHandler& _exceptionHandler) noexcept { exceptionHandler = _exceptionHandler; }

	public:
		[[nodiscard]] int_t Read(std::span<std::byte> _buffer);
		bool_t Write(std::span<const std::byte> _data);

		[[nodiscard]] int_t ReadFrom(std::span<std::byte> _buffer, sockaddr* _fromAddress, socklen_t* _fromAddressLength);
		bool_t WriteTo(std::span<const std::byte> _data, const sockaddr* _toAddress, socklen_t _toAddressLength);

	private:
		[[nodiscard]] static AddressInfo ResolveAddress(string_view_t _server, int_t _port);
		[[nodiscard]] SocketHandle CreateHandle() const;

	private:
		void_t ProcessReadEvent();
		void_t ProcessErrorEvent();

	public:
		[[nodiscard]] socket_t GetHandle() const { return handle.Get(); }
	};

END_NS

typedef ne::protocol::UdpSocket NebulaUdpSocket;

#endif //UDPSOCKET_H
