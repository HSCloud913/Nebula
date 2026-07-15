//
// Created by nebula on 24. 5. 17.
//

#include "Log/Logger.h"

#include <filesystem>
#include <format>
#include <iomanip>
#include <sstream>
#include <ctime>



using namespace std::chrono_literals;
namespace fs = std::filesystem;



BEGIN_NS(ne)
	Logger::Logger(const string_t& _fileName)
		: logLevel(LogLevel::NE_TRACE)
	{
		if (!Open(_fileName)) {}
		backendThread = std::thread(&Logger::BackendLoop, this);
	}

	Logger::Logger(const string_t& _filePath, const string_t& _fileName)
		: logLevel(LogLevel::NE_TRACE)
	{
		if (!Open(_filePath, _fileName)) {}
		backendThread = std::thread(&Logger::BackendLoop, this);
	}

	Logger::~Logger()
	{
		// running 변경은 wakeMutex(백엔드 wait 술어가 읽는 락) 하에서 하고 깨운다 — lost-wakeup 방지.
		{
			std::lock_guard<std::mutex> lock(wakeMutex);
			running.store(false, std::memory_order_relaxed);
		}
		wake.notify_one();

		if (backendThread.joinable()) backendThread.join();

		FlushPending(); // 백엔드 종료 후 남은 레코드 최종 드레인(파괴 중 동시 로깅은 계약 위반)
		Close();
	}



	bool_t Logger::Open(const string_t& _fileName)
	{
		std::lock_guard<std::mutex> lockGuard(mutex); // os 는 항상 락 하에서만 접근(비스레드안전 ofstream)

		if (os.is_open()) return true;

		fs::path path = fs::current_path();
		path /= _fileName;

		try
		{
			if (!fs::exists(path.parent_path())) fs::create_directories(path.parent_path());

			os.open(path, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
		} catch (const fs::filesystem_error&) { return false; }

		return os.is_open();
	}

	bool_t Logger::Open(const string_t& _filePath, const string_t& _fileName)
	{
		std::lock_guard<std::mutex> lockGuard(mutex); // os 는 항상 락 하에서만 접근(비스레드안전 ofstream)

		if (os.is_open()) return true;

		fs::path path(_filePath);
		path /= _fileName;

		try
		{
			if (!fs::exists(path.parent_path())) fs::create_directories(path.parent_path());

			os.open(path, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
		} catch (const fs::filesystem_error&) { return false; }

		return os.is_open();
	}

	bool_t Logger::Close()
	{
		std::lock_guard<std::mutex> lockGuard(mutex);

		if (os.is_open()) os.close();

		return !os.is_open();
	}

	bool_t Logger::IsOpen() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return os.is_open();
	}



	void_t Logger::Write(const LogLevel _logLevel, const string_t& _message)
	{
		if (_logLevel < logLevel.load(std::memory_order_relaxed)) return;

		queue.Enqueue(LogRecord{ _logLevel, _message, std::chrono::system_clock::now() });
		// Enqueue 완료 후 pending 세팅 → 백엔드가 놓쳐도(false-empty) 다음 wait 술어에서 재드레인.
		pending.store(true, std::memory_order_release);
		wake.notify_one();
	}

	void_t Logger::WriteToFile(const LogRecord& _record)
	{
		std::lock_guard<std::mutex> lockGuard(mutex);

		if (!os.is_open()) return;

		os << std::format("{} {} {}", GetDateTime(_record.timestamp), LogLevelToString(_record.level), _record.message) << '\n';

		if (_record.level >= LogLevel::NE_FATAL) os.flush();
	}

	void_t Logger::BackendLoop()
	{
		while (running.load(std::memory_order_relaxed))
		{
			// 유휴 시 스핀(1ms 폴링) 대신 condvar 로 블록. pending.exchange 로 lost-wakeup 을 흡수한다.
			{
				std::unique_lock<std::mutex> lock(wakeMutex);
				wake.wait(lock, [this] { return !running.load(std::memory_order_relaxed) || pending.exchange(false, std::memory_order_acq_rel); });
			}

			LogRecord record;
			while (queue.Dequeue(record)) WriteToFile(record);
		}
	}

	void_t Logger::FlushPending()
	{
		LogRecord record;
		while (queue.Dequeue(record)) WriteToFile(record);
	}



	string_t Logger::LogLevelToString(const LogLevel _logLevel)
	{
		switch (_logLevel)
		{
			case LogLevel::NE_TRACE:
				return "[TRACE]";
			case LogLevel::NE_DEBUG:
				return "[DEBUG]";
			case LogLevel::NE_INFO:
				return "[INFO]";
			case LogLevel::NE_WARNING:
				return "[WARNING]";
			case LogLevel::NE_ERROR:
				return "[ERROR]";
			case LogLevel::NE_FATAL:
				return "[FATAL]";
			default:
				return "";
		}
	}

	string_t Logger::GetDateTime(const std::chrono::time_point<std::chrono::system_clock> _timePoint)
	{
		const std::time_t now = std::chrono::system_clock::to_time_t(_timePoint);
		auto millisecond = std::chrono::duration_cast<std::chrono::milliseconds>(_timePoint.time_since_epoch()) % 1000;

		std::tm tm{};
#if defined(_WIN32)
		localtime_s(&tm, &now);
#else
		localtime_r(&now, &tm);
#endif

		std::ostringstream oss;
		oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << ":" << std::setw(3) << std::setfill('0') << millisecond.count() << "]";

		return oss.str();
	}

END_NS
