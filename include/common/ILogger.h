// include/common/ILogger.h
#pragma once

#include <string>
#include <iostream>

namespace common {

class ILogger {
public:
    virtual ~ILogger() = default;

    virtual void info(const std::string& msg) = 0;
    virtual void warn(const std::string& msg) = 0;
    virtual void error(const std::string& msg) = 0;
    virtual void debug(const std::string& msg) = 0;
};

/// Реализация-заглушка (no-op) для продакшена или тестов
class NullLogger : public ILogger {
public:
    void info(const std::string&) override {}
    void warn(const std::string&) override {}
    void error(const std::string&) override {}
    void debug(const std::string&) override {}
};

/// Простая реализация для вывода в консоль (опционально)
class ConsoleLogger : public ILogger {
public:
    void info(const std::string& msg) override { std::cout << "[INFO] " << msg << std::endl; }
    void warn(const std::string& msg) override { std::cout << "[WARN] " << msg << std::endl; }
    void error(const std::string& msg) override { std::cout << "[ERROR] " << msg << std::endl; }
    void debug(const std::string& msg) override { std::cout << "[DEBUG] " << msg << std::endl; }
};

} // namespace common