// src/LLMMessageHandler.cpp
#include "core/LLMMessageHandler.h"
#include "event/Event.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace core {

LLMMessageHandler::LLMMessageHandler(
    std::shared_ptr<IOpenAIChat> openAI,
    std::shared_ptr<common::ILogger> logger,
    const std::string& systemPrompt,
    int maxHistoryTokens)
    : openAI_(std::move(openAI))
    , logger_(std::move(logger))
    , systemPrompt_(systemPrompt)
    , maxHistoryTokens_(maxHistoryTokens)
    , historyFilePath_("data/history.json") {
    // Создаём директорию data/, если её нет
    std::filesystem::create_directories("data");
    loadHistory();
}

std::string LLMMessageHandler::handle(const event::Event& event) {
    const auto* msg = std::get_if<event::TelegramMessage>(&event.payload);
    if (!msg) {
        logger_->debug("LLMMessageHandler: non-Telegram event, ignoring");
        return "";
    }

    const std::string userText = msg->text;
    logger_->info("LLM request from chat " + std::to_string(msg->chatId) + ": " + userText);

    // Добавляем сообщение пользователя в историю
    history_.push_back({IOpenAIChat::Role::User, userText});
    trimHistoryByTokens();
    saveHistory();

    try {
        IOpenAIChat::Session session;
        session.systemPrompt = systemPrompt_;
        session.messages = history_;

        auto start = std::chrono::steady_clock::now();
        auto response = openAI_->chat(session);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        logger_->debug("LLM responded in " + std::to_string(elapsed.count()) + " ms, history tokens: " +
                       std::to_string(countHistoryTokens()));

        // Добавляем ответ ассистента в историю
        history_.push_back({IOpenAIChat::Role::Assistant, response});
        trimHistoryByTokens();
        saveHistory();

        return response;
    } catch (const std::exception& e) {
        logger_->error("LLM API error: " + std::string(e.what()));
        return "Возникла ошибка с интернет-подключением, попробуйте написать позже";
    }
}

int LLMMessageHandler::countTokens(const std::string& text) const {
    // Приблизительный подсчёт: 1 токен ≈ 3 символа (эмпирический)
    return static_cast<int>(text.size() / 3) + 1;
}

int LLMMessageHandler::countHistoryTokens() const {
    int total = 0;
    for (const auto& msg : history_) {
        total += countTokens(msg.content);
    }
    return total;
}

void LLMMessageHandler::trimHistoryByTokens() {
    int totalTokens = countHistoryTokens();
    if (totalTokens <= maxHistoryTokens_) {
        return;
    }

    logger_->debug("History tokens (" + std::to_string(totalTokens) +
                   ") exceed limit " + std::to_string(maxHistoryTokens_) +
                   ", trimming...");

    // Удаляем самые старые сообщения, пока не уложимся в лимит
    while (!history_.empty() && countHistoryTokens() > maxHistoryTokens_) {
        logger_->debug("Removing oldest message: " + history_.front().content);
        history_.erase(history_.begin());
    }

    logger_->debug("History trimmed to " + std::to_string(history_.size()) +
                   " messages (" + std::to_string(countHistoryTokens()) + " tokens)");
}

void LLMMessageHandler::saveHistory() const {
    try {
        using json = nlohmann::json;
        json j = json::array();
        for (const auto& msg : history_) {
            std::string roleStr;
            switch (msg.role) {
                case IOpenAIChat::Role::User: roleStr = "user"; break;
                case IOpenAIChat::Role::Assistant: roleStr = "assistant"; break;
                default: continue; // system не сохраняем
            }
            j.push_back({{"role", roleStr}, {"content", msg.content}});
        }

        std::ofstream file(historyFilePath_);
        if (!file.is_open()) {
            logger_->warn("Could not open history file for writing: " + historyFilePath_);
            return;
        }
        file << j.dump(2); // форматированный JSON с отступом 2
        logger_->debug("History saved to " + historyFilePath_);
    } catch (const std::exception& e) {
        logger_->warn("Failed to save history: " + std::string(e.what()));
    }
}

void LLMMessageHandler::loadHistory() {
    using json = nlohmann::json;
    std::ifstream file(historyFilePath_);
    if (!file.is_open()) {
        logger_->debug("History file not found, starting with empty history.");
        return;
    }

    try {
        json j;
        file >> j;
        if (!j.is_array()) {
            logger_->warn("History file is not a JSON array, ignoring.");
            return;
        }

        history_.clear();
        for (const auto& item : j) {
            if (!item.contains("role") || !item.contains("content")) {
                logger_->warn("Skipping invalid history entry (missing role or content)");
                continue;
            }
            std::string roleStr = item["role"];
            std::string content = item["content"];
            IOpenAIChat::Role role;
            if (roleStr == "user") role = IOpenAIChat::Role::User;
            else if (roleStr == "assistant") role = IOpenAIChat::Role::Assistant;
            else {
                logger_->warn("Skipping unknown role: " + roleStr);
                continue;
            }
            history_.push_back({role, content});
        }

        // Обрезаем историю по токенам, если превышен лимит
        trimHistoryByTokens();
        logger_->info("History loaded from " + historyFilePath_ +
                      " (" + std::to_string(history_.size()) + " messages)");
    } catch (const std::exception& e) {
        logger_->error("Failed to parse history file: " + std::string(e.what()) +
                       ". Starting with empty history.");
        history_.clear();
    }
}

} // namespace core