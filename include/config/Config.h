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
    std::string chat_access_mode;   // "papik_only", "pinned_only", "all"
};

struct LoggingConfig {
    std::string level;
};

struct LlmConfig {
    std::string endpoint;
    std::string api_key;
    std::string model;
    int max_history_tokens = 8000;
};

struct OutputConfig {
    std::string level; // none, tools, main, deep
};

struct Config {
    TelegramConfig telegram;
    AuthConfig auth;
    LoggingConfig logging;
    LlmConfig llm;
    OutputConfig output;
};

/// Загружает конфигурацию из файла config.toml.
/// Если файл не существует, создаёт его с дефолтными значениями.
Config load(const std::string& path = "config.toml");

} // namespace config