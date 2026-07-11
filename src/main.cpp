#include "config/Config.h"
#include "telegram/ITelegramClient.h"
#include "event/PriorityEventQueue.h"
#include "core/AgentMainLoop.h"
#include "core/EchoMessageHandler.h"
#include "common/ILogger.h"
#include "event/PriorityResolver.h"
#include "TelegramClient.h"

#include <csignal>
#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>

std::atomic<bool> shutdownRequested{false};

void signalHandler(int) {
    shutdownRequested = true;
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        // 1. Загружаем конфиг
        auto cfg = config::load();

        // 2. Создаём логгер
        auto logger = std::make_shared<common::ConsoleLogger>();

        // 3. Инициализируем Telegram-клиент
        auto client = std::make_shared<telegram::TelegramClient>();
        telegram::TdConfig tdConfig;
        tdConfig.api_id = cfg.telegram.api_id;
        tdConfig.api_hash = cfg.telegram.api_hash;
        tdConfig.database_directory = cfg.telegram.database_directory;

        logger->info("Connecting to Telegram...");
        client->connect(tdConfig).wait();
        logger->info("Connected.");

        // 4. Авторизация
        if (client->getAuthState() == telegram::AuthState::LoggedOut) {
            std::cout << "Enter phone number (e.g. +79123456789): ";
            std::string phone;
            std::cin >> phone;
            client->login(phone).wait();
        }

        // Ожидаем код подтверждения
        if (client->getAuthState() == telegram::AuthState::WaitingForCode) {
            std::cout << "Enter auth code: ";
            std::string code;
            std::cin >> code;
            client->setAuthCode(code).wait();
        }

        // Если запрошен пароль (двухфакторка)
        if (client->getAuthState() == telegram::AuthState::WaitingForPassword) {
            std::cout << "Enter 2FA password: ";
            std::string password;
            std::cin >> password;
            client->setPassword(password).wait();
        }

        // Дожидаемся, пока состояние станет LoggedIn
        while (client->getAuthState() != telegram::AuthState::LoggedIn) {
            logger->debug("Waiting for login...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        logger->info("Logged in successfully.");

        // 5. Очередь событий
        auto queue = std::make_shared<event::PriorityEventQueue>(logger);

        // 6. Резолвер приоритетов
        auto resolver = std::make_shared<event::PriorityResolver>();
        if (cfg.auth.papik_chat_id != 0) {
            resolver->setPapikChatId(cfg.auth.papik_chat_id);
            logger->info("Papik chat ID set to " + std::to_string(cfg.auth.papik_chat_id));
        }

        // 7. Обработчик (эхо)
        auto handler = std::make_shared<core::EchoMessageHandler>(logger);

        // 8. Основной цикл
        core::AgentMainLoop loop(client, queue, handler, resolver, logger);

        logger->info("Starting AgentMainLoop...");
        std::thread loopThread([&loop]() {
            loop.run();
        });

        // Ждём сигнал или завершение цикла
        while (!shutdownRequested && loop.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (shutdownRequested) {
            logger->info("Shutdown signal received, stopping loop...");
        }
        loop.stop();
        loopThread.join();

        logger->info("Application finished.");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}