//
// Created by nebula on 24. 5. 29.
//

#ifndef MESSAGEQUEUE_H
#define MESSAGEQUEUE_H

#include <memory>
#include <span>
#include <vector>

#include "Type.h"

BEGIN_NS(ne::protocol::Ipc)
	class MessageQueue final
	{
		NEBULA_NON_COPYABLE(MessageQueue)

	public:
		explicit MessageQueue(string_view_t _name);
		~MessageQueue();

		MessageQueue(MessageQueue&&) noexcept;
		MessageQueue& operator=(MessageQueue&&) noexcept;

	public:
		void_t Listen();
		void_t Connect();

	public:
		void_t Send(std::span<const std::byte> _message) const;
		[[nodiscard]] std::vector<std::byte> Receive() const;

	private:
		class Impl;
		std::unique_ptr<Impl> impl;
	};

END_NS

typedef ne::protocol::Ipc::MessageQueue NebulaMessageQueue;

#endif //MESSAGEQUEUE_H
