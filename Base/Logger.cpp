//
// Created by hsclo on 24. 5. 17.
//

#include "Logger.h"

#include <iostream>
#include <format>
#include <filesystem>



namespace fs = std::filesystem;



BEGIN_NS(ne)
    Logger::Logger(const string_t& _fileName)
        : logLevel(LogLevel::TRACE)
    {
        if (!Open(fs::current_path().string(), _fileName))
        {

        }
    }

    Logger::Logger(const string_t& _filePath, const string_t& _fileName)
        : logLevel(LogLevel::TRACE)
    {
        if (!Open(_filePath, _fileName))
        {

        }
    }



    LogLevel Logger::GetLogLevel() const
    {
        return logLevel;
    }

    void_t Logger::SetLogLevel(const LogLevel& _logLevel)
    {
        logLevel = _logLevel;
    }



    bool_t Logger::Open(const string_t& _fileName)
    {
        if (os.is_open()) return true;

        std::lock_guard<std::mutex> lockGuard(mutex);

        fs::path path(fs::current_path().string());
        path /= _fileName;

        try
        {
            if (!fs::exists(path.parent_path()))
            {
                fs::create_directories(path.parent_path());
                fs::permissions(path, std::filesystem::perms::all, std::filesystem::perm_options::remove);
            }

            os.open(path, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
        }
        catch (const fs::filesystem_error& e)
        {
            return false;
        }

        return os.is_open();
    }

    bool_t Logger::Open(const string_t& _filePath, const string_t& _fileName)
    {
        if (os.is_open()) return true;

        std::lock_guard<std::mutex> lockGuard(mutex);

        fs::path path(_filePath);
        path /= _fileName;

        try
        {
            if (!fs::exists(path.parent_path()))
            {
                fs::create_directories(path.parent_path());
                fs::permissions(path, std::filesystem::perms::all, std::filesystem::perm_options::remove);
            }

            os.open(path, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
        }
        catch (const fs::filesystem_error& e)
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



    void_t Logger::Trace(const string_t& _message)
    {
        Write(LogLevel::TRACE, _message);
    }

    void_t Logger::Debug(const string_t& _message)
    {
        Write(LogLevel::DBG, _message);
    }

    void_t Logger::Info(const string_t& _message)
    {
        Write(LogLevel::INFO, _message);
    }

    void_t Logger::Warning(const string_t& _message)
    {
        Write(LogLevel::WARN, _message);
    }

    void_t Logger::Error(const string_t& _message)
    {
        Write(LogLevel::ERR, _message);
    }

    void_t Logger::Fatal(const string_t& _message)
    {
        Write(LogLevel::FATAL, _message);
    }


    void_t Logger::Write(LogLevel _logLevel, const string_t& _message)
    {
        if (_logLevel < logLevel || !os.is_open()) return;

        std::lock_guard<std::mutex> lockGuard(mutex);

        os << std::format("{} {} {}",
                          GetDateTime(std::chrono::system_clock::now()),
                          LogLevelToString(_logLevel),
                          _message)
           << std::endl;
    }


    string_t Logger::LogLevelToString(LogLevel _logLevel)
    {
        switch (_logLevel)
        {
        case LogLevel::TRACE: return "[TRACE]";
        case LogLevel::DBG: return "[DEBUG]";
        case LogLevel::INFO: return "[INFO]";
        case LogLevel::WARN: return "[WARNING]";
        case LogLevel::ERR: return "[ERROR]";
        case LogLevel::FATAL: return "[FATAL]";
        }

        return "";
    }

    string_t Logger::GetDateTime(std::chrono::time_point<std::chrono::system_clock> _timePoint)
    {
        const std::time_t now = std::chrono::system_clock::to_time_t(_timePoint);
        auto millisecond = std::chrono::duration_cast<std::chrono::milliseconds>(_timePoint.time_since_epoch()) % 1000;

        std::ostringstream os;
        os << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S")
           << ":" << std::setw(3) << std::setfill('0') << millisecond.count() << "]";

        return os.str();
    }

END_NS
