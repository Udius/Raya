#include "config/Config.h"
#include <toml.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace config {

static toml::value createDefaultConfig() {
    // Создаём таблицу верхнего уровня
    toml::table config;

    // Вкладываем подтаблицы, используя toml::value(toml::table{...})
    config["telegram"] = toml::value(toml::table{
        {"api_id", 0},
        {"api_hash", ""},
        {"database_directory", "./tdlib_session"}
    });

    config["auth"] = toml::value(toml::table{
        {"papik_chat_id", 0}
    });

    config["logging"] = toml::value(toml::table{
        {"level", "info"}
    });

    return toml::value(std::move(config));
}

Config load(const std::string& path) {
    toml::value data;
    std::ifstream file(path);
    if (!file.is_open()) {
        auto defaultData = createDefaultConfig();
        std::ofstream out(path);
        if (!out.is_open()) {
            throw std::runtime_error("Unable to create config file: " + path);
        }
        // Записываем TOML в файл
        out << defaultData;
        std::cout << "[Config] Created default config.toml. Please edit it.\n";
        data = defaultData;
    } else {
        try {
            data = toml::parse(file);
        } catch (const std::exception& e) {
            std::cerr << "[Config] Error parsing config.toml: " << e.what() << "\n";
            throw;
        }
    }

    Config cfg;
    try {
        cfg.telegram.api_id = toml::find<int>(data, "telegram", "api_id");
        cfg.telegram.api_hash = toml::find<std::string>(data, "telegram", "api_hash");
        cfg.telegram.database_directory = toml::find<std::string>(data, "telegram", "database_directory");

        cfg.auth.papik_chat_id = toml::find<int64_t>(data, "auth", "papik_chat_id");

        cfg.logging.level = toml::find<std::string>(data, "logging", "level");
    } catch (const std::out_of_range& e) {
        std::cerr << "[Config] Missing required field: " << e.what() << "\n";
        throw;
    }
    return cfg;
}

} // namespace config