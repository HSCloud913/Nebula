//
// Created by nebula on 24. 5. 29.
//

#pragma once

#include <memory>
#include <span>
#include <vector>

#include "Coroutine/Task.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

// IIoEngine 전방 선언 — 헤더 체인을 최소화하기 위해 Engine/IIoEngine.h 는 .cpp 에서만 include
namespace ne::io { class IIoEngine; }

BEGIN_NS(ne::ipc)
	class MessageQueue final
	{
	public:
		explicit MessageQueue(string_view_t _name);
		~MessageQueue();

		MessageQueue(MessageQueue&&) noexcept;
		MessageQueue& operator=(MessageQueue&&) noexcept;

		NEBULA_NON_COPYABLE(MessageQueue)

	public:
		void_t Listen();
		void_t Connect();

	public:
		// 기존 동기 API — 호환성 유지
		void_t Send(std::span<const std::byte> _message) const;
		[[nodiscard]] std::vector<std::byte> Receive() const;

		// 비동기 API — IIoEngine 기반
		// POSIX: mqd_t 를 IIoEngine::Watch 로 직접 감시 (approach a)
		// Windows: bridge 스레드가 blocking ReadFile/WriteFile 수행 후 resume (approach b, engine 미사용)
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>>
			SendAsync(std::span<const std::byte> _message, ne::io::IIoEngine& _engine);
		[[nodiscard]] ne::Task<ne::Result<std::vector<std::byte>, ne::OsError>>
			ReceiveAsync(ne::io::IIoEngine& _engine);

	private:
		class Impl;
		std::unique_ptr<Impl> impl;
	};
