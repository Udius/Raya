#pragma once
#include "core/IMessageHandler.h"
#include "IOpenAIChat.h"
#include "common/ILogger.h"
#include "common/UserOutput.h"
#include "core/SelfModelManager.h"
#include "EmbeddingClient.h"

#include <memory>
#include <functional>
#include <map>
#include <vector>

namespace core {

class ToolRegistry {
public:
    using ToolHandler = std::function<std::string(const nlohmann::json&)>;
    void registerTool(const std::string& name, ToolHandler handler);
    std::string execute(const std::string& name, const nlohmann::json& args) const;
    bool hasTool(const std::string& name) const;
private:
    std::map<std::string, ToolHandler> handlers_;
};

class LLMMessageHandler : public IMessageHandler {
public:
    LLMMessageHandler(
        std::shared_ptr<IOpenAIChat> openAI,
        std::shared_ptr<common::ILogger> logger,
        const std::string& systemPrompt,
        int maxHistoryTokens,
        std::shared_ptr<ToolRegistry> toolRegistry,
        const std::vector<IOpenAIChat::Tool>& availableTools = {},
        std::shared_ptr<common::UserOutput> userOutput = nullptr,
        std::shared_ptr<SelfModelManager> selfModel = nullptr,
        std::shared_ptr<EmbeddingClient> embeddingClient = nullptr,
        int updateAfterNResponses = 5
    );
    std::string handle(const event::Event& event) override;

private:
    // Подсчёт токенов и обрезка истории
    int countTokens(const std::string& text) const;
    int countHistoryTokens() const;
    void trimHistoryByTokens();
    void trimHistoryByTokens(std::vector<IOpenAIChat::Message>& history) const; // перегрузка для временной истории

    // Сохранение/загрузка истории
    void saveHistory() const;
    void loadHistory();

    std::string buildSystemPrompt() const;

    // Поля
    std::shared_ptr<EmbeddingClient> embeddingClient_;
    int updateAfterNResponses_;
    int responseCount_ = 0;
    std::shared_ptr<SelfModelManager> selfModel_;
    std::shared_ptr<IOpenAIChat> openAI_;
    std::shared_ptr<common::ILogger> logger_;
    std::shared_ptr<common::UserOutput> userOutput_;
    std::string systemPrompt_;
    std::vector<IOpenAIChat::Message> history_;
    int maxHistoryTokens_;
    std::string historyFilePath_;
    std::shared_ptr<ToolRegistry> toolRegistry_;
    std::vector<IOpenAIChat::Tool> availableTools_;

    static constexpr int MAX_TOOL_ITERATIONS = 5;
};

} // namespace core