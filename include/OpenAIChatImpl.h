// include/OpenAIChatImpl.h
#pragma once

#include "IOpenAIChat.h"
#include <string>

class OpenAIChatImpl : public IOpenAIChat {
public:
    OpenAIChatImpl(const Endpoint& endpoint, const std::string& model);
    ~OpenAIChatImpl() override;

    std::string chat(const Session& session) override;

private:
    Endpoint endpoint_;
    std::string model_;
};