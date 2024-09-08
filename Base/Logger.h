//
// Created by hsclo on 24. 5. 17.
//

#ifndef NEBULALOGGER_H
#define NEBULALOGGER_H

#include <fstream>
#include <mutex>
#include "Type.h"

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

    class Logger final
    {
    public:
        explicit Logger(const string_t& _fileName);
        explicit Logger(const string_t& _filePath, const string_t& _fileName);
        ~Logger() = default;

    private:
        std::mutex mutex;
        std::ofstream os;
        LogLevel logLevel;
        string_t date;

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

    private:
        static string_t LogLevelToString(LogLevel _logLevel);
        static string_t GetDateTime(std::chrono::time_point<std::chrono::system_clock> _timePoint);
    };
END_NS

typedef ne::Logger NebulaLogger;
typedef ne::LogLevel NebulaLogLevel;

#endif //NEBULALOGGER_H
