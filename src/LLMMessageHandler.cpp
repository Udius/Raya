// src/LLMMessageHandler.cpp
#include "core/LLMMessageHandler.h"
#include "event/Event.h"
#include <chrono>

namespace core {

LLMMessageHandler::LLMMessageHandler(
    std::shared_ptr<IOpenAIChat> openAI,
    std::shared_ptr<common::ILogger> logger,
    const std::string& systemPrompt,
    int maxHistoryTokens)
    : openAI_(std::move(openAI))
    , logger_(std::move(logger))
    , systemPrompt_(systemPrompt)
    , maxHistoryTokens_(maxHistoryTokens) {}

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

} // namespace core