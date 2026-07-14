// include/OpenAIChatImpl.h
#pragma once

#include "IOpenAIChat.h"
#include <string>

class OpenAIChatImpl : public IOpenAIChat {
public:
    OpenAIChatImpl(const Endpoint& endpoint, const std::string& model);
    ~OpenAIChatImpl() override = default;

    ChatResponse chat(const Session& session, const std::vector<Tool>& tools = {}) override;

private:
    Endpoint endpoint_;
    std::string model_;
};