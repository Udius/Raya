#include "core/AgentMainLoop.h"

#include "event/Event.h"
#include "event/IEventQueue.h"
#include "common/ILogger.h"

#include <cstdint>
#include <string>
#include <future>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <utility>

namespace core {

// ------------------------------------------------------------------
// Конструктор
// ------------------------------------------------------------------
AgentMainLoop::AgentMainLoop(
    std::shared_ptr<telegram::ITelegramClient> client,
    std::shared_ptr<event::IEventQueue> queue,
    std::shared_ptr<IMessageHandler> handler,
    std::shared_ptr<event::PriorityResolver> resolver,
    int64_t papikChatId,
    const std::string& accessMode,
    std::shared_ptr<common::ILogger> logger,
    std::shared_ptr<common::UserOutput> userOutput)
    : client_(std::move(client))
    , queue_(std::move(queue))
    , handler_(std::move(handler))
    , resolver_(std::move(resolver))
    , papikChatId_(papikChatId)
    , accessMode_(accessMode)
    , logger_(logger ? logger : std::make_shared<common::NullLogger>())
    , userOutput_(std::move(userOutput)) {}

// ------------------------------------------------------------------
// Деструктор (опционально, но для полноты)
// ------------------------------------------------------------------
AgentMainLoop::~AgentMainLoop() {
    stop();
}

// ------------------------------------------------------------------
// run() – запуск цикла (блокирующий)
// ------------------------------------------------------------------
void AgentMainLoop::run() {
    if (running_.exchange(true)) {
        logger_->warn("AgentMainLoop::run() called while already running");
        return;
    }
    shouldStop_ = false;
    logger_->info("AgentMainLoop started");

    setupTelegramListener();
    processEvents();

    logger_->info("AgentMainLoop stopped");
    running_ = false;
}

// ------------------------------------------------------------------
// stop() – остановка цикла (неблокирующий)
// ------------------------------------------------------------------
void AgentMainLoop::stop() {
    if (!running_.load()) {
        logger_->warn("stop() called on stopped loop");
        return;
    }
    logger_->info("Stopping AgentMainLoop...");
    shouldStop_ = true;
    queue_->shutdown();
    // Если run() выполняется в отдельном потоке, можно дождаться его завершения,
    // но мы не создаём поток внутри класса, поэтому ничего не делаем.
}

// ------------------------------------------------------------------
// setupTelegramListener() – подписка на входящие сообщения
// ------------------------------------------------------------------
void AgentMainLoop::setupTelegramListener() {
    client_->addMessageListener([this](int64_t chatId, int64_t messageId, const std::string& text) {
        // Фильтрация по доступу
        if (!isChatAllowed(chatId)) {
            logger_->warn("Message from chat " + std::to_string(chatId) + 
                          " ignored (access mode: " + accessMode_ + ")");
            return;
        }
        event::TelegramMessage msg{chatId, messageId, text};
        auto priority = resolver_ ? resolver_->resolve(chatId) : event::EventPriority::Normal;
        event::Event ev(priority, "telegram", std::move(msg));
        queue_->push(std::move(ev));
        logger_->debug("Pushed message from chat " + std::to_string(chatId));
    });
}

// ------------------------------------------------------------------
// processEvents() – основной цикл обработки
// ------------------------------------------------------------------
void AgentMainLoop::processEvents() {
    while (!shouldStop_.load()) {
        try {
            event::Event ev = queue_->pop();
            handleEvent(ev);
        } catch (const event::ShutdownException&) {
            logger_->debug("Queue shutdown detected, exiting loop");
            break;
        } catch (const std::exception& e) {
            logger_->error("Error in processEvents: " + std::string(e.what()));
        } catch (...) {
            logger_->error("Unknown error in processEvents");
        }
    }
}

// ------------------------------------------------------------------
// handleEvent() – обработка одного события
// ------------------------------------------------------------------
void AgentMainLoop::handleEvent(const event::Event& ev) {
    logger_->debug("Event popped, source=" + ev.source);

    std::string reply = handler_->handle(ev);
    if (reply.empty()) {
        return;
    }

    const auto* msg = std::get_if<event::TelegramMessage>(&ev.payload);
    if (!msg) {
        logger_->warn("Event is not TelegramMessage, cannot reply");
        return;
    }

    // Асинхронная отправка с таймаутом (не блокируем цикл надолго)
    // Асинхронная отправка с таймаутом
    try {
        auto future = client_->sendMessage(msg->chatId, reply);
        auto status = future.wait_for(std::chrono::seconds(1));
        if (status == std::future_status::ready) {
            future.get();
            logger_->debug("Sent reply to " + std::to_string(msg->chatId));
        } else {
            logger_->warn("Send timeout, message may not be delivered");
        }
    } catch (const std::exception& e) {
        logger_->error("Send failed: " + std::string(e.what()));
    }
}

bool AgentMainLoop::isChatAllowed(int64_t chatId) const {
    if (accessMode_ == "papik_only") {
        return chatId == papikChatId_;
    }
    if (accessMode_ == "pinned_only") {
        return client_->isChatPinned(chatId);
    }
    // "all" или неизвестный режим
    return true;
}

} // namespace core