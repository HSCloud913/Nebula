//
// Created by hscloud on 26. 7. 24.
//

#include "Io/Listener.h"

#include <utility>

#if defined(_WIN32)
#include "Base/WinsockApi.h"
#elif defined(IS_POSIX)
#   include <arpa/inet.h>
#   include <netinet/in.h>
#   include <sys/socket.h>
#endif



namespace ne::io
{
	namespace
	{
		// 주소 문자열이 IPv6 리터럴이면 AF_INET6, 아니면 AF_INET 으로 본다.
		int_t InferFamily(const string_view_t _ip) noexcept
		{
			const string_t ip(_ip);
			in6_addr probe{};
			return ::inet_pton(AF_INET6, ip.c_str(), &probe) == 1 ? AF_INET6 : AF_INET;
		}
	}



	uint16_t Listener::LocalPort() const noexcept
	{
		sockaddr_storage address{};
#if defined(_WIN32)
		int length = static_cast<int>(sizeof(address));
#else
		socklen_t length = static_cast<socklen_t>(sizeof(address));
#endif
		if (::getsockname(socket.Handle(), reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0;

		if (address.ss_family == AF_INET6) return ::ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
		return ::ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
	}



	IoResult<Listener> Listener::Bind(Context& _context, const string_view_t _ip, const uint16_t _port, const int_t _backlog)
	{
		auto created = Socket::Create(_context, InferFamily(_ip));
		if (created.IsError()) return IoResult<Listener>::Error(std::move(created.Error()));

		Socket socket = std::move(created.Value());

		if (auto bound = socket.Bind(_ip, _port); bound.IsError()) return IoResult<Listener>::Error(std::move(bound.Error()));
		if (auto listened = socket.Listen(_backlog); listened.IsError()) return IoResult<Listener>::Error(std::move(listened.Error()));

		return IoResult<Listener>::Ok(Listener{ std::move(socket) });
	}
}
