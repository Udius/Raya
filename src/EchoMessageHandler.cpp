// src/EchoMessageHandler.cpp
#include "core/EchoMessageHandler.h"   // заголовок с объявлением класса
#include "event/Event.h"
#include "common/ILogger.h"

namespace core {

EchoMessageHandler::EchoMessageHandler(std::shared_ptr<common::ILogger> logger)
    : logger_(logger ? logger : std::make_shared<common::NullLogger>()) {}

std::string EchoMessageHandler::handle(const event::Event& event) {
    const auto* msg = std::get_if<event::TelegramMessage>(&event.payload);
    if (!msg) {
        logger_->debug("Ignoring non-Telegram event");
        return "";
    }
    return "Echo: " + msg->text;
}

} // namespace core