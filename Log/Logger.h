//
// Created by nebula on 24. 5. 17.
//

#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include "Type.h"
#include "Queue/MpscQueue.h"

BEGIN_NS(ne)
    enum class LogLevel
    {
        TRACE,
        DBG,
        INFO,
        WARN,
        ERR,
        FATAL
    };

    struct LogRecord
    {
        LogLevel level{ LogLevel::TRACE };
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
        std::mutex mutex;
        std::ofstream os;
        std::atomic<LogLevel> logLevel;

        ne::concurrency::MpscQueue<LogRecord> queue;
        std::thread backendThread;
        std::atomic<bool_t> running{ true };

    public:
        LogLevel GetLogLevel() const;
        void_t SetLogLevel(const LogLevel& _logLevel);

    public:
        [[nodiscard]] bool_t Open(const string_t& _fileName);
        [[nodiscard]] bool_t Open(const string_t& _filePath, const string_t& _fileName);
        [[nodiscard]] bool_t Close();
        [[nodiscard]] bool_t IsOpen() const;

    public:
        void_t Trace(const string_t& _message);
        void_t Debug(const string_t& _message);
        void_t Info(const string_t& _message);
        void_t Warning(const string_t& _message);
        void_t Error(const string_t& _message);
        void_t Fatal(const string_t& _message);

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
