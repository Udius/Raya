#pragma once

#include <string>
#include <cstdint>

namespace config {

struct TelegramConfig {
    int api_id;
    std::string api_hash;
    std::string database_directory;
};

struct AuthConfig {
    int64_t papik_chat_id;
};

struct LoggingConfig {
    std::string level;
};

struct Config {
    TelegramConfig telegram;
    AuthConfig auth;
    LoggingConfig logging;
};

/// Загружает конфигурацию из файла config.toml.
/// Если файл не существует, создаёт его с дефолтными значениями.
Config load(const std::string& path = "config.toml");

} // namespace config