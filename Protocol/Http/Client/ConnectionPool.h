//
// Created by nebula on 26. 6. 25.
//

#ifndef HTTPCLIENTCONNECTIONPOOL_H
#define HTTPCLIENTCONNECTIONPOOL_H

#include <mutex>
#include <unordered_map>
#include <vector>
#include <optional>

#include "Interface/ISingleton.h"
#include "Socket.h"

BEGIN_NS(ne::protocol::Http::Client)
	class ConnectionPool final :public ISingleton<ConnectionPool>
	{
		friend class ISingleton<ConnectionPool>;

	private:
		explicit ConnectionPool() = default;

	public:
		~ConnectionPool() override = default;

	private:
		std::mutex mutex;
		std::unordered_map<string_t, std::vector<Socket>> idleSockets;

	public:
		[[nodiscard]] static string_t MakeKey(string_view_t _host, int_t _port, bool_t _isTlsEncrypted);

		[[nodiscard]] std::optional<Socket> Acquire(const string_t& _key);
		void_t Release(const string_t& _key, Socket&& _socket);
	};

END_NS

#endif //HTTPCLIENTCONNECTIONPOOL_H
