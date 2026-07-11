// include/core/EchoMessageHandler.h
#pragma once

#include "core/IMessageHandler.h"
#include "common/ILogger.h"
#include <memory>

namespace core {

class EchoMessageHandler : public IMessageHandler {
public:
    explicit EchoMessageHandler(std::shared_ptr<common::ILogger> logger = nullptr);

    std::string handle(const event::Event& event) override;

private:
    std::shared_ptr<common::ILogger> logger_;
};

} // namespace core