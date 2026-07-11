// include/core/AgentMainLoop.h
#pragma once

#include "telegram/ITelegramClient.h"
#include "event/IEventQueue.h"
#include "core/IMessageHandler.h"
#include "common/ILogger.h"
#include "event/PriorityResolver.h"

#include <memory>
#include <atomic>
#include <thread>

namespace core {

/**
 * @brief Основной цикл агента.
 * Подписывается на сообщения Telegram, помещает их в очередь,
 * обрабатывает через IMessageHandler и отправляет ответ.
 */
class AgentMainLoop {
public:
    /**
     * @param client   Telegram-клиент (реализация ITelegramClient)
     * @param queue    Очередь событий (реализация IEventQueue)
     * @param handler  Обработчик сообщений
     * @param logger   Логгер (опционально, по умолчанию NullLogger)
     */
    AgentMainLoop(
        std::shared_ptr<telegram::ITelegramClient> client,
        std::shared_ptr<event::IEventQueue> queue,
        std::shared_ptr<IMessageHandler> handler,
        std::shared_ptr<event::PriorityResolver> resolver,
        std::shared_ptr<common::ILogger> logger = std::make_shared<common::NullLogger>()
    );
    
    ~AgentMainLoop();

    /// Запустить основной цикл (блокирующий). Возвращает только после вызова stop() или ошибки.
    void run();

    /// Остановить цикл (неблокирующий). Вызывает shutdown() очереди.
    void stop();

    /// Проверить, работает ли цикл.
    [[nodiscard]] bool isRunning() const { return running_.load(); }

private:
    std::shared_ptr<event::PriorityResolver> resolver_;
    std::shared_ptr<telegram::ITelegramClient> client_;
    std::shared_ptr<event::IEventQueue> queue_;
    std::shared_ptr<IMessageHandler> handler_;
    std::shared_ptr<common::ILogger> logger_;

    std::atomic<bool> running_{false};
    std::atomic<bool> shouldStop_{false};
    std::thread workerThread_;

    /// Настройка подписки на входящие сообщения Telegram.
    void setupTelegramListener();

    /// Основной цикл обработки событий.
    void processEvents();

    /// Отправить ответное сообщение.
    void sendReply(int64_t chatId, const std::string& text);
    
    void handleEvent(const event::Event& ev);
};

} // namespace core