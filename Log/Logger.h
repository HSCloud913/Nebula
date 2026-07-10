//
// Created by nebula on 24. 5. 17.
//

#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include "Base/Type.h"
#include "Concurrency/Queue/MpscQueue.h"

BEGIN_NS(ne)
	enum class LogLevel
	{
		NE_TRACE,
		NE_DEBUG,
		NE_INFO,
		NE_WARNING,
		NE_ERROR,
		NE_FATAL
	};

	struct LogRecord
	{
		LogLevel level{ LogLevel::NE_TRACE };
		string_t message;
		std::chrono::system_clock::time_point timestamp;
	};

	class Logger final
	{
	public:
		explicit Logger(const string_t& _fileName);
		explicit Logger(const string_t& _filePath, const string_t& _fileName);
		~Logger();

	private:
		mutable std::mutex mutex; // os(파일) 보호. IsOpen() const 에서도 잠그므로 mutable.
		std::ofstream os;
		std::atomic<LogLevel> logLevel;

		ne::concurrency::MpscQueue<LogRecord> queue;
		std::thread backendThread;
		std::atomic<bool_t> running{ true };

		// 백엔드 웨이크업: 1ms 폴링 대신 condvar 로 대기한다. pending 플래그(exchange-in-predicate)로
		// lost-wakeup 과 MPSC 의 순간적 false-empty(생산자 enqueue 도중) 를 함께 방어한다 —
		// 생산자가 Enqueue 완료 후 pending=true 를 세우므로, 그 사이 놓친 레코드는 다음 wait 에서 재드레인된다.
		std::mutex wakeMutex;
		std::condition_variable wake;
		std::atomic<bool_t> pending{ false };

	public:
		LogLevel GetLogLevel() const { return logLevel.load(std::memory_order_relaxed); }
		void_t SetLogLevel(const LogLevel& _logLevel) { logLevel.store(_logLevel, std::memory_order_relaxed); }

	public:
		[[nodiscard]] bool_t Open(const string_t& _fileName);
		[[nodiscard]] bool_t Open(const string_t& _filePath, const string_t& _fileName);
		[[nodiscard]] bool_t Close();
		[[nodiscard]] bool_t IsOpen() const;

	public:
		void_t Trace(const string_t& _message) { Write(LogLevel::NE_TRACE, _message); }
		void_t Debug(const string_t& _message) { Write(LogLevel::NE_DEBUG, _message); }
		void_t Info(const string_t& _message) { Write(LogLevel::NE_INFO, _message); }
		void_t Warning(const string_t& _message) { Write(LogLevel::NE_WARNING, _message); }
		void_t Error(const string_t& _message) { Write(LogLevel::NE_ERROR, _message); }
		void_t Fatal(const string_t& _message) { Write(LogLevel::NE_FATAL, _message); }

	private:
		void_t Write(LogLevel _logLevel, const string_t& _message);
		void_t WriteToFile(const LogRecord& _record);
		void_t BackendLoop();
		void_t FlushPending();

	private:
		static string_t LogLevelToString(LogLevel _logLevel);
		static string_t GetDateTime(std::chrono::time_point<std::chrono::system_clock> _timePoint);
	};

END_NS
