// src/LLMMessageHandler.cpp
#include "core/LLMMessageHandler.h"
#include "event/Event.h"
#include <chrono>

namespace core {

LLMMessageHandler::LLMMessageHandler(
    std::shared_ptr<IOpenAIChat> openAI,
    std::shared_ptr<common::ILogger> logger,
    const std::string& systemPrompt)
    : openAI_(std::move(openAI))
    , logger_(std::move(logger))
    , systemPrompt_(systemPrompt) {}

std::string LLMMessageHandler::handle(const event::Event& event) {
    // Извлекаем текст только из TelegramMessage
    const auto* msg = std::get_if<event::TelegramMessage>(&event.payload);
    if (!msg) {
        logger_->debug("LLMMessageHandler: non-Telegram event, ignoring");
        return "";
    }

    const std::string userText = msg->text;
    logger_->info("LLM request from chat " + std::to_string(msg->chatId) + ": " + userText);

    // Добавляем сообщение пользователя в историю
    history_.push_back({IOpenAIChat::Role::User, userText});
    if (history_.size() > MAX_HISTORY) {
        history_.erase(history_.begin());
    }

    try {
        // Формируем сессию: системный промпт + история
        IOpenAIChat::Session session;
        session.systemPrompt = systemPrompt_;
        session.messages = history_;

        auto start = std::chrono::steady_clock::now();
        auto response = openAI_->chat(session);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        logger_->debug("LLM responded in " + std::to_string(elapsed.count()) + " ms");

        // Добавляем ответ ассистента в историю
        history_.push_back({IOpenAIChat::Role::Assistant, response});
        if (history_.size() > MAX_HISTORY) {
            history_.erase(history_.begin());
        }

        return response;
    } catch (const std::exception& e) {
        logger_->error("LLM API error: " + std::string(e.what()));
        return "Извините, произошла ошибка. Попробуйте позже.";
    }
}

} // namespace core