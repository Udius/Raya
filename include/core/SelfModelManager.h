#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <chrono>
#include <memory>

// Добавляем инклюды для common
#include "common/ILogger.h"
#include "common/UserOutput.h"
#include "IOpenAIChat.h"
#include "EmbeddingClient.h" 

namespace core {

struct SelfModelState {
    int version = 1;
    std::string last_updated;
    std::vector<float> state_embedding;
    std::string state_description;
    std::vector<std::string> interests;
    std::vector<std::string> goals;
};

class SelfModelManager {
public:
    SelfModelManager(const std::string& filePath = "../data/self_model.json");
    void load();
    void save(const SelfModelState& state);
    const SelfModelState& getState() const { return state_; }
    bool hasState() const { return !state_.state_description.empty(); }

    std::string getDescription() const { return state_.state_description; }
    std::vector<std::string> getInterests() const { return state_.interests; }
    std::vector<std::string> getGoals() const { return state_.goals; }

    void update(const std::vector<IOpenAIChat::Message>& history,
                std::shared_ptr<IOpenAIChat> chat,
                std::shared_ptr<EmbeddingClient> embeddingClient,
                std::shared_ptr<common::ILogger> logger,
                std::shared_ptr<common::UserOutput> userOutput = nullptr);

private:
    std::vector<float> mergeEmbeddings(const std::vector<float>& oldEmbedding,
                                       const std::vector<float>& newEmbedding) const;

    std::string filePath_;
    SelfModelState state_;
};

} // namespace core