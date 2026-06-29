//
// Created by nebula on 24. 5. 29.
//

#ifndef TCPSOCKET_H
#define TCPSOCKET_H

#include <memory>
#include <functional>
#include <queue>
#include <chrono>

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
	public:
		explicit TcpSocket(socket_t _socket) noexcept : handle(_socket) {}
		TcpSocket(string_view_t _server, int_t _port);

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
		std::chrono::milliseconds timeout{ 0 };

	private:
		std::queue<std::span<const std::byte>> writeQueue;

		ReadHandler readHandler;
		ExceptionHandler exceptionHandler;

	public:
		void_t Connect();
		void_t Reconnect();
		[[nodiscard]] bool_t IsConnected() const;
		[[nodiscard]] bool_t IsAlive();
		void_t Bind();
		void_t Listen();
		[[nodiscard]] socket_t Accept(sockaddr* _sockAddr = nullptr, socklen_t* _addrLength = nullptr);

		void_t Select();
		void_t Poll();
#if defined(_WIN32)
		void_t Iocp();
#elif defined(__linux__)
		void_t Epoll();
#elif defined(__APPLE__)
		void_t Kqueue();
#endif

	public:
		void_t SetSocketOption(int _option, const char* _value, int _valueLength) const;
		void_t SocketControl(long_t _option, ulong_t _value, bool _isEnable = true) const;

		void_t SetSocketMode(bool _isNonblocking);
		void_t SetTimeout(std::chrono::milliseconds _timeout);

	public:
		void_t RegisterReadHandler(const ReadHandler& _readHandler) noexcept { readHandler = _readHandler; }
		void_t RegisterExceptionHandler(const ExceptionHandler& _exceptionHandler) noexcept { exceptionHandler = _exceptionHandler; }

	public:
		[[nodiscard]] longlong_t Read(std::span<std::byte> _buffer);
		bool_t Write(std::span<const std::byte> _data);

	private:
		[[nodiscard]] SocketHandle CreateHandle() const;
		[[nodiscard]] AddressInfo GetAddressInfo(string_view_t _server, int_t _port);

	private:
		void_t ProcessReadEvent();
		void_t ProcessWriteEvent();
		void_t ProcessErrorEvent();

	public:
		[[nodiscard]] socket_t GetHandle() const { return handle.Get(); }
		[[nodiscard]] socket_t ReleaseHandle() noexcept;
	};

END_NS

typedef ne::protocol::TcpSocket NebulaTcpSocket;

#endif //TCPSOCKET_H
