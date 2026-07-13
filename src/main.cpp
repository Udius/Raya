#include "config/Config.h"
#include "telegram/ITelegramClient.h"
#include "event/PriorityEventQueue.h"
#include "core/AgentMainLoop.h"
#include "core/EchoMessageHandler.h"
#include "common/ILogger.h"
#include "event/PriorityResolver.h"
#include "TelegramClient.h"
#include "OpenAIChatImpl.h"
#include "core/LLMMessageHandler.h"

#include <csignal>
#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <cstring>

std::atomic<bool> shutdownRequested{false};
std::atomic<bool> connected{false};

void signalHandler(int) {
    // Минимальный безопасный вывод для сигнала
    const char* msg = "*** SIGNAL HANDLER CALLED ***\n";
    write(STDERR_FILENO, msg, strlen(msg));
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
        logger->info("Connected successfully.");
        connected = true;
    } catch (const std::exception& e) {
        logger->error("Connection failed: " + std::string(e.what()));
    }
}

// Поток авторизации
void runAuthorization(std::shared_ptr<telegram::ITelegramClient> client,
                      std::shared_ptr<common::ILogger> logger,
                      std::atomic<bool>& shutdownRequested) {
    try {
        while (!shutdownRequested && client->getAuthState() != telegram::AuthState::LoggedIn) {
            auto state = client->getAuthState();
            logger->debug("Authorization state: " + std::to_string(static_cast<int>(state)));

            if (state == telegram::AuthState::LoggedOut || state == telegram::AuthState::WaitingForPhoneNumber) {
                std::cout << "Enter phone number (e.g. +79123456789): " << std::flush;
                std::string phone;
                std::cin >> phone;
                if (std::cin.eof() || std::cin.fail() || shutdownRequested) return;
                logger->info("Sending phone number...");
                auto future = client->login(phone);
                auto status = future.wait_for(std::chrono::seconds(30));
                if (status == std::future_status::timeout) {
                    logger->error("Login timeout");
                    return;
                }
                future.get();
            } else if (state == telegram::AuthState::WaitingForCode) {
                std::cout << "Enter auth code: " << std::flush;
                std::string code;
                std::cin >> code;
                if (std::cin.eof() || std::cin.fail() || shutdownRequested) return;
                logger->info("Sending authentication code...");
                auto future = client->setAuthCode(code);
                auto status = future.wait_for(std::chrono::seconds(30));
                if (status == std::future_status::timeout) {
                    logger->error("Authentication code timeout");
                    return;
                }
                future.get();
            } else if (state == telegram::AuthState::WaitingForPassword) {
                std::cout << "Enter 2FA password: " << std::flush;
                std::string password;
                std::cin >> password;
                if (std::cin.eof() || std::cin.fail() || shutdownRequested) return;
                logger->info("Sending 2FA password...");
                auto future = client->setPassword(password);
                auto status = future.wait_for(std::chrono::seconds(30));
                if (status == std::future_status::timeout) {
                    logger->error("2FA password timeout");
                    return;
                }
                future.get();
            } else if (state == telegram::AuthState::Error) {
                logger->error("Authentication error, please restart the application.");
                return;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        if (!shutdownRequested) {
            logger->info("Logged in successfully.");
        }
    } catch (const std::exception& e) {
        if (!shutdownRequested) {
            logger->error("Authorization error: " + std::string(e.what()));
        }
    }
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        auto logger = std::make_shared<common::ConsoleLogger>(
            common::ConsoleLogger::fromString("info") // можно заменить на cfg.logging.level после загрузки
        );

        logger->info("Loading configuration...");
        auto cfg = config::load();
        // Обновляем уровень логирования из конфига
        logger = std::make_shared<common::ConsoleLogger>(
            common::ConsoleLogger::fromString(cfg.logging.level)
        );

        logger->info("Initializing Telegram client...");
        auto client = std::make_shared<telegram::TelegramClient>();
        client->setLogger(logger);

        telegram::TdConfig tdConfig;
        tdConfig.api_id = cfg.telegram.api_id;
        tdConfig.api_hash = cfg.telegram.api_hash;
        tdConfig.database_directory = cfg.telegram.database_directory;

        logger->info("Starting connection thread...");
        std::thread connectThread(runConnect, client, tdConfig, logger);

        if (shutdownRequested) {
            if (connectThread.joinable()) {
                connectThread.detach();
            }
            logger->info("Application terminated early.");
            return 0;
        }

        if (connectThread.joinable()) {
            connectThread.join();
        }

        if (!connected) {
            logger->error("Failed to connect to Telegram. Exiting.");
            return 1;
        }

        logger->info("Starting authorization...");
        std::thread authThread(runAuthorization, client, logger, std::ref(shutdownRequested));

        logger->info("Initializing event queue...");
        auto queue = std::make_shared<event::PriorityEventQueue>(logger);

        logger->info("Initializing priority resolver...");
        auto resolver = std::make_shared<event::PriorityResolver>();
        if (cfg.auth.papik_chat_id != 0) {
            resolver->setPapikChatId(cfg.auth.papik_chat_id);
            logger->info("Papik chat ID set to " + std::to_string(cfg.auth.papik_chat_id));
        }

        logger->info("Creating message handler...");
        // auto handler = std::make_shared<core::EchoMessageHandler>(logger);
        // Загружаем настройки LLM
        auto llmEndpoint = cfg.llm.endpoint;
        auto llmApiKey = cfg.llm.api_key;
        auto llmModel = cfg.llm.model;

        // Создаём OpenAIChatImpl с этими параметрами
        auto openAI = std::make_shared<OpenAIChatImpl>(
            IOpenAIChat::Endpoint{cfg.llm.endpoint, cfg.llm.api_key},
            cfg.llm.model
        );

        // Системный промпт (позже вынести в файл)
        const std::string systemPrompt = 
            "Ты — Рая, виртуальный ассистент с живым и дружелюбным характером. "
            "Отвечай на вопросы кратко, по делу, но с теплотой. Ты помогаешь пользователю, "
            "общаешься на русском языке, если пользователь пишет на русском, иначе на языке пользователя.";

        auto handler = std::make_shared<core::LLMMessageHandler>(openAI, logger, systemPrompt);

        logger->info("Starting main agent loop...");
        core::AgentMainLoop loop(
            client, 
            queue, 
            handler, 
            resolver, 
            cfg.auth.papik_chat_id, 
            cfg.auth.chat_access_mode, 
            logger
        );
        std::thread loopThread([&loop]() { loop.run(); });

        logger->info("Agent is running. Press Ctrl+C to stop.");
        while (!shutdownRequested) {
            pause();  // ждём сигнал
        }

        logger->info("Shutdown requested. Stopping...");

        // Принудительное завершение через 3 секунды
        std::thread force_exit_timer([&]() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            const char* msg = "!!! FORCED EXIT !!!\n";
            write(STDERR_FILENO, msg, strlen(msg));
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
                logger->warn("Loop thread did not stop, detaching.");
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
                logger->warn("Auth thread did not stop, detaching.");
                authThread.detach();
            } else {
                authThread.join();
            }
        }

        logger->info("Application finished successfully.");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}