// include/IOpenAIChat.h
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

class IOpenAIChat {
public:
    struct Endpoint { std::string baseUrl; std::string bearerKey; };
    enum class Role { System, User, Assistant, Tool };   // добавили Tool
    struct Message {
        Role role;
        std::string content;
        std::string tool_call_id;  // для сообщений с ролью Tool
    };

    struct Session {
        std::string systemPrompt;
        std::vector<Message> messages;
    };

    // Структуры для инструментов (function calling)
    struct ToolFunction {
        std::string name;
        std::string description;
        nlohmann::json parameters; // JSON schema
    };
    struct Tool {
        std::string type; // "function"
        ToolFunction function;
    };
    struct ToolCall {
        std::string id;
        std::string type; // "function"
        std::string function_name;
        nlohmann::json arguments;
    };
    struct ChatResponse {
        std::string content;                  // если есть текстовый ответ
        std::vector<ToolCall> tool_calls;     // если есть вызовы инструментов
        bool has_tool_calls() const { return !tool_calls.empty(); }
    };

    virtual ~IOpenAIChat() = default;

    // Основной метод с поддержкой инструментов
    virtual ChatResponse chat(const Session& session, const std::vector<Tool>& tools = {}) = 0;
};

// Базовая реализация OpenAIChatImpl (если ещё нет)
#include <nlohmann/json.hpp>
#include <httplib.h> // или другой HTTP-клиент
// ... реализация