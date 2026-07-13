// include/IOpenAIChat.h
#pragma once
#include <string>
#include <vector>
#include <optional>

class IOpenAIChat {
public:
    struct Endpoint {
        std::string baseUrl;
        std::string bearerKey;
    };

    enum class Role { System, User, Assistant };

    struct Message {
        Role role;
        std::string content;
    };

    struct Session {
        std::string systemPrompt;
        std::vector<Message> messages;
    };

    virtual ~IOpenAIChat() = default;

    /// Выполнить запрос к LLM и вернуть ответ ассистента
    virtual std::string chat(const Session& session) = 0;
};

// Базовая реализация OpenAIChatImpl (если ещё нет)
#include <nlohmann/json.hpp>
#include <httplib.h> // или другой HTTP-клиент
// ... реализация