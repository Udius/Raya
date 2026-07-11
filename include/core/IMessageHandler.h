// include/core/IMessageHandler.h
#pragma once

#include "event/Event.h"
#include <string>

namespace core {

/**
 * @brief Интерфейс обработчика событий.
 * Получает событие, возвращает текст ответа (или пустую строку, если ответ не требуется).
 */
class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;

    /// Обработать событие и вернуть текст для отправки обратно.
    /// Возвращает пустую строку, если ответ не нужен.
    virtual std::string handle(const event::Event& event) = 0;
};

} // namespace core