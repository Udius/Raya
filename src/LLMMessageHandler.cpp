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
#include <chrono>

namespace core {

LLMMessageHandler::LLMMessageHandler(
    std::shared_ptr<IOpenAIChat> openAI,
    std::shared_ptr<common::ILogger> logger,
    const std::string& systemPrompt,
    int maxHistoryTokens,
    std::shared_ptr<ToolRegistry> toolRegistry,
    const std::vector<IOpenAIChat::Tool>& availableTools,
    std::shared_ptr<common::UserOutput> userOutput)
    : openAI_(std::move(openAI))
    , logger_(std::move(logger))
    , userOutput_(std::move(userOutput))
    , systemPrompt_(systemPrompt)
    , maxHistoryTokens_(maxHistoryTokens)
    , historyFilePath_("data/history.json")
    , toolRegistry_(std::move(toolRegistry))
    , availableTools_(availableTools) {
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
    if (userOutput_) {
        userOutput_->onMessageReceived(userText, msg->chatId);
        userOutput_->onThinkingStart();
    }
    
    // Добавляем сообщение пользователя в историю
    history_.push_back({IOpenAIChat::Role::User, userText, ""});
    trimHistoryByTokens();
    saveHistory();

    // Копия истории для работы (чтобы не сохранять промежуточные состояния)
    std::vector<IOpenAIChat::Message> tempHistory = history_;

    int iterations = 0;
    std::string finalAnswer;

    while (iterations < MAX_TOOL_ITERATIONS) {
        IOpenAIChat::Session session{systemPrompt_, tempHistory};
        
        // Измеряем время выполнения запроса
        auto start = std::chrono::steady_clock::now();
        auto response = openAI_->chat(session, availableTools_);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        );
        
        // Выводим время мышления
        if (userOutput_) {
            userOutput_->onThinkingDone(elapsed.count() / 1000.0);
        }

        if (response.has_tool_calls()) {
            logger_->debug("LLM requested " + std::to_string(response.tool_calls.size()) +
                           " tool calls, iteration " + std::to_string(iterations+1));

            // Добавляем сообщение ассистента с tool_calls в историю
            tempHistory.push_back({IOpenAIChat::Role::Assistant, "Calling tools...", ""});

            for (const auto& toolCall : response.tool_calls) {
                std::string toolResult;
                try {
                    if (!toolRegistry_->hasTool(toolCall.function_name)) {
                        throw std::runtime_error("Unknown tool: " + toolCall.function_name);
                    }
                    toolResult = toolRegistry_->execute(toolCall.function_name, toolCall.arguments);
                    logger_->debug("Tool " + toolCall.function_name + " executed successfully");
                    
                    // Выводим информацию о вызове инструмента
                    if (userOutput_) {
                        userOutput_->onToolCall(toolCall.function_name, toolCall.arguments.dump());
                        userOutput_->onToolResult(toolResult);
                    }
                } catch (const std::exception& e) {
                    logger_->error("Tool execution error: " + std::string(e.what()));
                    toolResult = "Error: " + std::string(e.what());
                    if (userOutput_) {
                        userOutput_->onToolCall(toolCall.function_name, toolCall.arguments.dump());
                        userOutput_->onToolResult("Error: " + std::string(e.what()));
                    }
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
            
            // Выводим ответ пользователю
            if (userOutput_) {
                userOutput_->onReplySent(finalAnswer);
            }
            return finalAnswer;
        }

        // Не должно случиться, но на всякий случай
        logger_->warn("Empty response from LLM, retrying...");
        iterations++;
    }

    // Превышен лимит итераций
    logger_->error("Max tool iterations exceeded (" + std::to_string(MAX_TOOL_ITERATIONS) + ")");
    std::string errorMsg = "Извините, произошла ошибка при обработке запроса. Попробуйте позже.";
    if (userOutput_) {
        userOutput_->onThinkingError("Max tool iterations exceeded");
        userOutput_->onReplySent(errorMsg);
    }
    return errorMsg;
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