//
// Created by nebula on 26. 6. 25.
//

#include "ConnectionPool.h"

#include <format>

BEGIN_NS(ne::protocol::Http::Client)
	namespace
	{
		constexpr std::size_t MaxIdlePerKey = 4;
	}

	string_t ConnectionPool::MakeKey(const string_view_t _host, const int_t _port, const bool_t _isTlsEncrypted)
	{
		return std::format("{}:{}:{}", _host, _port, _isTlsEncrypted ? 1 : 0);
	}

	std::optional<Socket> ConnectionPool::Acquire(const string_t& _key)
	{
		const auto lock = std::scoped_lock(mutex);

		const auto it = idleSockets.find(_key);
		if (it == idleSockets.end()) return {};

		while (!it->second.empty())
		{
			auto socket = std::move(it->second.back());
			it->second.pop_back();

			if (socket.IsAlive()) return socket;
		}

		idleSockets.erase(it);
		return {};
	}

	void_t ConnectionPool::Release(const string_t& _key, Socket&& _socket)
	{
		const auto lock = std::scoped_lock(mutex);

		auto& pool = idleSockets[_key];
		if (pool.size() >= MaxIdlePerKey) return;

		pool.push_back(std::move(_socket));
	}

END_NS
