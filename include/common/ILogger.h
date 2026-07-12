// include/common/ILogger.h
#pragma once

#include <string>
#include <iostream>
#include <map>

namespace common {

enum class LogLevel {
    Debug,   // все сообщения
    Info,    // info, warn, error
    Warn,    // warn, error
    Error    // только error
};

class ILogger {
public:
    virtual ~ILogger() = default;

    virtual void info(const std::string& msg) = 0;
    virtual void warn(const std::string& msg) = 0;
    virtual void error(const std::string& msg) = 0;
    virtual void debug(const std::string& msg) = 0;
};

/// Реализация-заглушка (no-op)
class NullLogger : public ILogger {
public:
    void info(const std::string&) override {}
    void warn(const std::string&) override {}
    void error(const std::string&) override {}
    void debug(const std::string&) override {}
};

/// Консольный логгер с фильтрацией по уровню
class ConsoleLogger : public ILogger {
public:
    explicit ConsoleLogger(LogLevel level = LogLevel::Info) : level_(level) {}

    // Преобразование строки в LogLevel (удобно для конфига)
    static LogLevel fromString(const std::string& levelStr) {
        static const std::map<std::string, LogLevel> mapping = {
            {"debug", LogLevel::Debug},
            {"info",  LogLevel::Info},
            {"warn",  LogLevel::Warn},
            {"error", LogLevel::Error}
        };
        auto it = mapping.find(levelStr);
        if (it != mapping.end()) return it->second;
        return LogLevel::Info; // значение по умолчанию
    }

    void info(const std::string& msg) override {
        if (level_ <= LogLevel::Info) std::cout << "[INFO] " << msg << std::endl;
    }
    void warn(const std::string& msg) override {
        if (level_ <= LogLevel::Warn) std::cout << "[WARN] " << msg << std::endl;
    }
    void error(const std::string& msg) override {
        if (level_ <= LogLevel::Error) std::cerr << "[ERROR] " << msg << std::endl;
    }
    void debug(const std::string& msg) override {
        if (level_ <= LogLevel::Debug) std::cout << "[DEBUG] " << msg << std::endl;
    }

private:
    LogLevel level_;
};

} // namespace common