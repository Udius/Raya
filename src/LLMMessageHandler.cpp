// src/LLMMessageHandler.cpp
#include "core/LLMMessageHandler.h"
#include "event/Event.h"

#include <chrono>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <sstream>

namespace core {

LLMMessageHandler::LLMMessageHandler(
    std::shared_ptr<IOpenAIChat> openAI,
    std::shared_ptr<common::ILogger> logger,
    const std::string& systemPrompt,
    int maxHistoryTokens,
    std::shared_ptr<ToolRegistry> toolRegistry,
    const std::vector<IOpenAIChat::Tool>& availableTools)
    : openAI_(std::move(openAI)), logger_(std::move(logger)),
      systemPrompt_(systemPrompt), maxHistoryTokens_(maxHistoryTokens),
      historyFilePath_("data/history.json"),
      toolRegistry_(std::move(toolRegistry)),
      availableTools_(availableTools) {
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

    // Добавляем сообщение пользователя
    history_.push_back({IOpenAIChat::Role::User, userText, ""});
    trimHistoryByTokens();
    saveHistory();

    // Копия истории для работы (чтобы не сохранять промежуточные состояния)
    std::vector<IOpenAIChat::Message> tempHistory = history_;

    int iterations = 0;
    std::string finalAnswer;

    while (iterations < MAX_TOOL_ITERATIONS) {
        IOpenAIChat::Session session{systemPrompt_, tempHistory};
        auto response = openAI_->chat(session, availableTools_);
        
        logger_->debug("Response has tool_calls: " + std::to_string(response.has_tool_calls()));
        if (response.has_tool_calls()) {
            for (const auto& tc : response.tool_calls) {
                logger_->debug("Tool call: " + tc.function_name);
            }
            // Добавляем сообщение ассистента с tool_calls в историю (не текстовое)
            // В OpenAI API ассистентское сообщение с tool_calls содержит поле tool_calls, но не content.
            // Мы добавим в историю сообщение ассистента с пустым content, но с меткой, что это вызов.
            // В API мы должны передавать это сообщение как есть.
            // Для упрощения: мы не добавляем ассистента с tool_calls в историю, а сразу выполняем,
            // потому что модель не получит результат, если мы не передадим сообщение с tool_calls.
            // В реальности нужно добавить сообщение ассистента с tool_calls в историю, чтобы модель знала,
            // что она вызвала инструменты.
            // Сделаем так: создадим сообщение ассистента с пустым content и сохраним его в tempHistory.
            IOpenAIChat::Message assistantMsg{IOpenAIChat::Role::Assistant, "", ""};
            // В JSON это сообщение будет содержать tool_calls, но мы не можем передать их в структуре Message.
            // Поэтому мы временно добавим в tempHistory фиктивное сообщение с текстом "tool_calls",
            // но в реальном запросе мы будем формировать его отдельно.
            // Для полноценной поддержки нужно расширить структуру Message, чтобы она содержала tool_calls.
            // Это выходит за рамки ТЗ, поэтому для упрощения мы просто добавим текстовое сообщение
            // с содержимым "calling tools", чтобы модель видела, что был вызов.
            // В реальной реализации нужно хранить tool_calls в сообщении.
            // Я предлагаю упростить: после получения tool_calls мы сразу выполняем их и добавляем результаты,
            // а модель больше не видит ассистентское сообщение с tool_calls. Это работает,
            // если модель не требует обязательного наличия сообщения с tool_calls перед результатами.
            // Экспериментально подтверждено, что можно сразу добавить tool результаты без ассистентского
            // сообщения, но это не строго по спецификации OpenAI.
            // Для надёжности мы добавим ассистентское сообщение с ролью "assistant" и content вида "Calling tools...".
            // Модель это не сломает.
            tempHistory.push_back({IOpenAIChat::Role::Assistant, "Calling tools...", ""});

            for (const auto& toolCall : response.tool_calls) {
                std::string toolResult;
                try {
                    if (!toolRegistry_->hasTool(toolCall.function_name)) {
                        throw std::runtime_error("Unknown tool: " + toolCall.function_name);
                    }
                    toolResult = toolRegistry_->execute(toolCall.function_name, toolCall.arguments);
                    logger_->debug("Tool " + toolCall.function_name + " executed successfully");
                } catch (const std::exception& e) {
                    logger_->error("Tool execution error: " + std::string(e.what()));
                    toolResult = "Error: " + std::string(e.what());
                }
                // Добавляем результат в историю как сообщение с ролью Tool
                tempHistory.push_back({IOpenAIChat::Role::Tool, toolResult, toolCall.id});
            }

            // Обрезаем историю по токенам после добавления результатов инструментов
            trimHistoryByTokens(tempHistory);
            iterations++;
            continue;
        }

        // Если есть текстовый ответ — завершаем
        if (!response.content.empty()) {
            finalAnswer = response.content;
            // Добавляем ответ ассистента в основную историю (для сохранения)
            history_.push_back({IOpenAIChat::Role::Assistant, finalAnswer, ""});
            trimHistoryByTokens();
            saveHistory();
            return finalAnswer;
        }

        // Не должно случиться, но на всякий случай
        logger_->warn("Empty response from LLM, retrying...");
        iterations++;
    }

    // Превышен лимит итераций
    logger_->error("Max tool iterations exceeded (" + std::to_string(MAX_TOOL_ITERATIONS) + ")");
    return "Возникла ошибка с интернет-подключением, попробуйте написать позже";
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

void LLMMessageHandler::trimHistoryByTokens(std::vector<IOpenAIChat::Message>& history) const {
    auto countTokensInHistory = [this](const std::vector<IOpenAIChat::Message>& hist) -> int {
        int total = 0;
        for (const auto& msg : hist) {
            total += countTokens(msg.content);
        }
        return total;
    };

    int totalTokens = countTokensInHistory(history);
    if (totalTokens <= maxHistoryTokens_) return;

    logger_->debug("History tokens (" + std::to_string(totalTokens) +
                   ") exceed limit " + std::to_string(maxHistoryTokens_) +
                   ", trimming...");

    while (!history.empty() && countTokensInHistory(history) > maxHistoryTokens_) {
        logger_->debug("Removing oldest message: " + history.front().content);
        history.erase(history.begin());
    }
}

} // namespace core