// include/event/Event.h
#pragma once

#include "EventPriority.h"
#include <chrono>
#include <string>
#include <cstdint>
#include <variant>

namespace event {

// ----- Базовые типы событий (можно расширять) -----
struct TelegramMessage {
    int64_t chatId;
    int64_t messageId;
    std::string text;
};

struct TimerEvent {
    std::string tag;
};

struct InternalEvent {
    std::string command;
};

// ----- Основное событие -----
struct Event {
    EventPriority priority;
    std::chrono::steady_clock::time_point timestamp;
    std::string source;   // "telegram", "timer", "internal", ...
    std::variant<TelegramMessage, TimerEvent, InternalEvent> payload;

    // Конструкторы для удобства создания
    Event() = default;

    Event(EventPriority p, const std::string& src, TelegramMessage msg)
        : priority(p)
        , timestamp(std::chrono::steady_clock::now())
        , source(src)
        , payload(std::move(msg)) {}

    Event(EventPriority p, const std::string& src, TimerEvent ev)
        : priority(p)
        , timestamp(std::chrono::steady_clock::now())
        , source(src)
        , payload(std::move(ev)) {}

    Event(EventPriority p, const std::string& src, InternalEvent ev)
        : priority(p)
        , timestamp(std::chrono::steady_clock::now())
        , source(src)
        , payload(std::move(ev)) {}

    // Вспомогательный метод для получения текста из TelegramMessage (если payload таков)
    std::string getText() const {
        if (const auto* msg = std::get_if<TelegramMessage>(&payload)) {
            return msg->text;
        }
        return {};
    }
};

} // namespace event