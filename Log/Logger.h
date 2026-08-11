//
// Created by nebula on 24. 5. 17.
//

#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "Base/Type.h"
#include "Concurrency/Queue/MpscQueue.h"
#include "Log/LogRecord.h"
#include "Log/Sink/ISink.h"

namespace ne::log
{
	/**
	 * @class Logger
	 * @brief 하나 이상의 ISink 로 로그를 비동기 기록하는 로거입니다.
	 *
	 * 호출 스레드는 Trace()~Fatal() 로 LogRecord 를 MpscQueue 에 밀어넣기만 하고, 단일 백엔드
	 * 스레드가 이를 꺼내 각 sink 에 기록합니다. 레벨 필터는 호출 스레드에서 즉시 적용됩니다.
	 * FATAL 은 즉시 flush 되며, Flush() 로 지금까지의 로그를 강제로 내보낼 수 있습니다(크래시 세이프티).
	 *
	 * @note 파일 전용이던 이전 API(Open/Close/IsOpen)는 제거되었습니다 — 출력 대상은 sink 가 관리합니다.
	 */
	class Logger final
	{
	public:
		explicit Logger(std::unique_ptr<ISink> _sink);
		explicit Logger(std::vector<std::unique_ptr<ISink>> _sinks);
		explicit Logger(const string_t& _fileName); // 편의: 단일 FileSink
		~Logger();

		NEBULA_NON_COPYABLE_MOVABLE(Logger)

	private:
		std::vector<std::unique_ptr<ISink>> sinks;
		std::atomic<LogLevel> logLevel{ LogLevel::NE_TRACE };

	private:
		ne::concurrency::MpscQueue<LogRecord> queue;
		std::thread backendThread;
		std::atomic<bool_t> isRunning{ true };

	private:
		std::mutex wakeMutex; // lost-wakeup 및 MPSC 순간적 false-empty 방어(생산자가 Enqueue 후 isPending 세팅).
		std::condition_variable wake;
		std::atomic<bool_t> isPending{ false };

	private:
		// Flush() 세대 핸드셰이크: 호출자가 flushRequest 를 올리고, 백엔드가 드레인+flush 후 flushComplete 를 맞춘다.
		std::atomic<ulonglong_t> flushRequest{ 0 };
		std::atomic<ulonglong_t> flushComplete{ 0 };

	public:
		[[nodiscard]] LogLevel GetLogLevel() const noexcept { return logLevel.load(std::memory_order_relaxed); }
		void_t SetLogLevel(const LogLevel _logLevel) noexcept { logLevel.store(_logLevel, std::memory_order_relaxed); }

	public:
		void_t Trace(const string_t& _message) { Write(LogLevel::NE_TRACE, _message); }
		void_t Debug(const string_t& _message) { Write(LogLevel::NE_DEBUG, _message); }
		void_t Info(const string_t& _message) { Write(LogLevel::NE_INFO, _message); }
		void_t Warning(const string_t& _message) { Write(LogLevel::NE_WARNING, _message); }
		void_t Error(const string_t& _message) { Write(LogLevel::NE_ERROR, _message); }
		void_t Fatal(const string_t& _message) { Write(LogLevel::NE_FATAL, _message); }

		/** @brief 지금까지 큐에 쌓인 레코드를 백엔드가 모두 sink 에 기록하고 flush 할 때까지 블록한다. */
		void_t Flush();

	private:
		void_t Write(LogLevel _logLevel, const string_t& _message);
		void_t BackendLoop();
		void_t DrainToSinks();
		void_t FlushSinks();
	};
}

namespace ne
{
	using Logger = log::Logger;
}
