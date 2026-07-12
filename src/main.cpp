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
#include <unistd.h>   // close, pause, _exit, write
#include <cstring>

std::atomic<bool> shutdownRequested{false};
std::atomic<bool> connected{false};

// Прямая запись в stderr без буферизации
void safe_stderr(const char* msg) {
    write(STDERR_FILENO, msg, strlen(msg));
}

void signalHandler(int) {
    safe_stderr("*** SIGNAL HANDLER CALLED ***\n");
    shutdownRequested = true;
    close(STDIN_FILENO);
}

// Функция для выполнения в отдельном потоке – попытка подключения
void runConnect(std::shared_ptr<telegram::ITelegramClient> client,
                const telegram::TdConfig& tdConfig,
                std::shared_ptr<common::ILogger> logger) {
    try {
        logger->info("Connecting to Telegram...");
        client->connect(tdConfig).wait();
        logger->info("Connected.");
        connected = true;
    } catch (const std::exception& e) {
        logger->error("Connection failed: " + std::string(e.what()));
    }
}

// Поток авторизации (без изменений)
void runAuthorization(std::shared_ptr<telegram::ITelegramClient> client,
                      std::shared_ptr<common::ILogger> logger,
                      std::atomic<bool>& shutdownRequested) {
    try {
        if (client->getAuthState() == telegram::AuthState::LoggedOut && !shutdownRequested) {
            std::cout << "Enter phone number (e.g. +79123456789): " << std::flush;
            std::string phone;
            std::cin >> phone;
            if (std::cin.eof() || std::cin.fail() || shutdownRequested) return;
            client->login(phone).wait();
        }
        if (client->getAuthState() == telegram::AuthState::WaitingForCode && !shutdownRequested) {
            std::cout << "Enter auth code: " << std::flush;
            std::string code;
            std::cin >> code;
            if (std::cin.eof() || std::cin.fail() || shutdownRequested) return;
            client->setAuthCode(code).wait();
        }
        if (client->getAuthState() == telegram::AuthState::WaitingForPassword && !shutdownRequested) {
            std::cout << "Enter 2FA password: " << std::flush;
            std::string password;
            std::cin >> password;
            if (std::cin.eof() || std::cin.fail() || shutdownRequested) return;
            client->setPassword(password).wait();
        }
        while (client->getAuthState() != telegram::AuthState::LoggedIn && !shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            logger->debug("Waiting for login...");
        }
        if (shutdownRequested) return;
        logger->info("Logged in successfully.");
    } catch (const std::exception& e) {
        if (!shutdownRequested) {
            logger->error("Authorization error: " + std::string(e.what()));
        }
    }
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    safe_stderr("MAIN: start\n");

    try {
        safe_stderr("MAIN: loading config...\n");
        auto cfg = config::load();
        safe_stderr("MAIN: config loaded\n");

        auto logger = std::make_shared<common::ConsoleLogger>(
            common::ConsoleLogger::fromString(cfg.logging.level)
        );
        safe_stderr("MAIN: logger created\n");

        auto client = std::make_shared<telegram::TelegramClient>();
        safe_stderr("MAIN: client created\n");

        telegram::TdConfig tdConfig;
        tdConfig.api_id = cfg.telegram.api_id;
        tdConfig.api_hash = cfg.telegram.api_hash;
        tdConfig.database_directory = cfg.telegram.database_directory;

        // Запускаем подключение в фоновом потоке
        safe_stderr("MAIN: starting connectThread...\n");
        std::thread connectThread(runConnect, client, tdConfig, logger);

        // Ждём либо успешного подключения, либо сигнала завершения
        while (!connected && !shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (shutdownRequested) {
            safe_stderr("MAIN: shutdown requested before connect finished.\n");
            if (connectThread.joinable()) {
                connectThread.detach();
            }
            logger->info("Application finished (early shutdown).");
            return 0;
        }

        safe_stderr("MAIN: client connected\n");
        if (connectThread.joinable()) {
            connectThread.join();
        }

        safe_stderr("MAIN: starting authThread...\n");
        std::thread authThread(runAuthorization, client, logger, std::ref(shutdownRequested));
        safe_stderr("MAIN: authThread started\n");

        safe_stderr("MAIN: creating PriorityEventQueue...\n");
        auto queue = std::make_shared<event::PriorityEventQueue>(logger);
        safe_stderr("MAIN: PriorityEventQueue created\n");

        safe_stderr("MAIN: creating PriorityResolver...\n");
        auto resolver = std::make_shared<event::PriorityResolver>();
        safe_stderr("MAIN: PriorityResolver created\n");

        if (cfg.auth.papik_chat_id != 0) {
            resolver->setPapikChatId(cfg.auth.papik_chat_id);
            logger->info("Papik chat ID set to " + std::to_string(cfg.auth.papik_chat_id));
        }

        safe_stderr("MAIN: creating EchoMessageHandler...\n");
        auto handler = std::make_shared<core::EchoMessageHandler>(logger);
        safe_stderr("MAIN: EchoMessageHandler created\n");

        safe_stderr("MAIN: creating AgentMainLoop...\n");
        core::AgentMainLoop loop(client, queue, handler, resolver, logger);
        safe_stderr("MAIN: AgentMainLoop created\n");

        logger->info("Starting AgentMainLoop...");
        safe_stderr("MAIN: starting loopThread...\n");
        std::thread loopThread([&loop]() { loop.run(); });
        safe_stderr("MAIN: loopThread started\n");

        safe_stderr("MAIN: entering pause loop, waiting for Ctrl+C...\n");
        while (!shutdownRequested) {
            pause();  // приостановка до получения сигнала
        }

        safe_stderr("MAIN: shutdown requested, stopping...\n");

        // Принудительное завершение через 3 секунды
        std::thread force_exit_timer([&]() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            safe_stderr("!!! FORCED EXIT after 3 seconds !!!\n");
            _exit(0);
        });
        force_exit_timer.detach();

        logger->info("Disconnecting Telegram client...");
        client->disconnect();

        loop.stop();
        if (loopThread.joinable()) {
            auto start = std::chrono::steady_clock::now();
            while (loopThread.joinable() &&
                   std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (loopThread.joinable()) {
                safe_stderr("MAIN: loopThread did not stop, detaching\n");
                loopThread.detach();
            } else {
                loopThread.join();
            }
        }

        if (authThread.joinable()) {
            auto start = std::chrono::steady_clock::now();
            while (authThread.joinable() &&
                   std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (authThread.joinable()) {
                safe_stderr("MAIN: authThread did not stop, detaching\n");
                authThread.detach();
            } else {
                authThread.join();
            }
        }

        logger->info("Application finished.");
        safe_stderr("MAIN: returning 0\n");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}