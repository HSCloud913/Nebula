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

	/**
	 * @class Logger
	 * @brief 파일에 로그를 비동기로 기록하는 로거입니다.
	 *
	 * 호출 스레드는 Trace()~Fatal() 로 LogRecord를 MpscQueue에 밀어넣기만 하고,
	 * 별도의 백엔드 스레드가 이를 꺼내 실제 파일 I/O(WriteToFile)를 수행합니다.
	 * 백엔드는 1ms 폴링 대신 condition_variable로 대기하며, 다중 스레드에서 동시에
	 * 로그를 기록해도 안전합니다.
	 */
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

	private:
		ne::concurrency::MpscQueue<LogRecord> queue;
		std::thread backendThread;
		std::atomic<bool_t> isRunning{ true };

	private:
		std::mutex wakeMutex; // lost-wakeup 과 MPSC 의 순간적 false-empty(생산자 enqueue 도중) 를 함께 방어한다. (생산자가 Enqueue 완료 후 pending=true 를 세우므로, 그 사이 놓친 레코드는 다음 wait 에서 다시 드레인)
		std::condition_variable wake;
		std::atomic<bool_t> isPending{ false };

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
