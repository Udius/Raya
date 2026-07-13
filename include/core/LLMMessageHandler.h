// include/core/LLMMessageHandler.h
#pragma once
#include "core/IMessageHandler.h"
#include "IOpenAIChat.h"
#include "common/ILogger.h"
#include <memory>

namespace core {

class LLMMessageHandler : public IMessageHandler {
public:
    LLMMessageHandler(
        std::shared_ptr<IOpenAIChat> openAI,
        std::shared_ptr<common::ILogger> logger,
        const std::string& systemPrompt
    );

    std::string handle(const event::Event& event) override;

private:
    std::shared_ptr<IOpenAIChat> openAI_;
    std::shared_ptr<common::ILogger> logger_;
    std::string systemPrompt_;
    // История диалога (храним последние N сообщений)
    std::vector<IOpenAIChat::Message> history_;
    static constexpr size_t MAX_HISTORY = 20;
};

} // namespace core