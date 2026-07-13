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
        const std::string& systemPrompt,
        int maxHistoryTokens
    );

    std::string handle(const event::Event& event) override;

private:
    int countTokens(const std::string& text) const;
    int countHistoryTokens() const;
    void trimHistoryByTokens();

    // Сохранение/загрузка истории
    void saveHistory() const;
    void loadHistory();

    std::shared_ptr<IOpenAIChat> openAI_;
    std::shared_ptr<common::ILogger> logger_;
    std::string systemPrompt_;
    std::vector<IOpenAIChat::Message> history_;
    int maxHistoryTokens_;
    std::string historyFilePath_;   // "data/history.json"
};

} // namespace core