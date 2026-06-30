//
// Created by nebula on 24. 5. 17.
//

#include "Logger.h"

#include <filesystem>
#include <format>
#include <iomanip>
#include <sstream>
#include <ctime>

using namespace std::chrono_literals;
namespace fs = std::filesystem;



BEGIN_NS(ne)
    Logger::Logger(const string_t& _fileName)
        : logLevel(LogLevel::TRACE)
    {
        if (!Open(_fileName)) {}
        backendThread = std::thread(&Logger::BackendLoop, this);
    }

    Logger::Logger(const string_t& _filePath, const string_t& _fileName)
        : logLevel(LogLevel::TRACE)
    {
        if (!Open(_filePath, _fileName)) {}
        backendThread = std::thread(&Logger::BackendLoop, this);
    }

    Logger::~Logger()
    {
        running.store(false, std::memory_order_relaxed);
        if (backendThread.joinable()) backendThread.join();
        FlushPending();
        Close();
    }



    LogLevel Logger::GetLogLevel() const
    {
        return logLevel.load(std::memory_order_relaxed);
    }

    void_t Logger::SetLogLevel(const LogLevel& _logLevel)
    {
        logLevel.store(_logLevel, std::memory_order_relaxed);
    }



    bool_t Logger::Open(const string_t& _fileName)
    {
        if (os.is_open()) return true;

        std::lock_guard<std::mutex> lockGuard(mutex);

        if (os.is_open()) return true;

        fs::path path = fs::current_path();
        path /= _fileName;

        try
        {
            if (!fs::exists(path.parent_path()))
                fs::create_directories(path.parent_path());

            os.open(path, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
        } catch (const fs::filesystem_error&)
        {
            return false;
        }

        return os.is_open();
    }

    bool_t Logger::Open(const string_t& _filePath, const string_t& _fileName)
    {
        if (os.is_open()) return true;

        std::lock_guard<std::mutex> lockGuard(mutex);

        if (os.is_open()) return true;

        fs::path path(_filePath);
        path /= _fileName;

        try
        {
            if (!fs::exists(path.parent_path()))
                fs::create_directories(path.parent_path());

            os.open(path, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
        } catch (const fs::filesystem_error&)
        {
            return false;
        }

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
        return os.is_open();
    }



    void_t Logger::Trace(const string_t& _message)   { Write(LogLevel::TRACE, _message); }
    void_t Logger::Debug(const string_t& _message)   { Write(LogLevel::DBG,   _message); }
    void_t Logger::Info(const string_t& _message)    { Write(LogLevel::INFO,  _message); }
    void_t Logger::Warning(const string_t& _message) { Write(LogLevel::WARN,  _message); }
    void_t Logger::Error(const string_t& _message)   { Write(LogLevel::ERR,   _message); }
    void_t Logger::Fatal(const string_t& _message)   { Write(LogLevel::FATAL, _message); }



    void_t Logger::Write(const LogLevel _logLevel, const string_t& _message)
    {
        if (_logLevel < logLevel.load(std::memory_order_relaxed)) return;

        queue.Enqueue(LogRecord{
            _logLevel,
            _message,
            std::chrono::system_clock::now()
        });
    }

    void_t Logger::WriteToFile(const LogRecord& _record)
    {
        std::lock_guard<std::mutex> lockGuard(mutex);

        if (!os.is_open()) return;

        os << std::format("{} {} {}",
                        GetDateTime(_record.timestamp),
                        LogLevelToString(_record.level),
                        _record.message)
           << '\n';

        if (_record.level >= LogLevel::FATAL) os.flush();
    }

    void_t Logger::BackendLoop()
    {
        while (running.load(std::memory_order_relaxed))
        {
            LogRecord record;
            if (queue.Dequeue(record))
                WriteToFile(record);
            else
                std::this_thread::sleep_for(1ms);
        }
    }

    void_t Logger::FlushPending()
    {
        LogRecord record;
        while (queue.Dequeue(record))
            WriteToFile(record);
    }



    string_t Logger::LogLevelToString(LogLevel _logLevel)
    {
        switch (_logLevel)
        {
        case LogLevel::TRACE: return "[TRACE]";
        case LogLevel::DBG:   return "[DEBUG]";
        case LogLevel::INFO:  return "[INFO]";
        case LogLevel::WARN:  return "[WARNING]";
        case LogLevel::ERR:   return "[ERROR]";
        case LogLevel::FATAL: return "[FATAL]";
        }

        return "";
    }

    string_t Logger::GetDateTime(std::chrono::time_point<std::chrono::system_clock> _timePoint)
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
        oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
            << ":" << std::setw(3) << std::setfill('0') << millisecond.count() << "]";

        return oss.str();
    }

END_NS
