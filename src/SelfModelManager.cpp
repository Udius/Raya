#include "core/SelfModelManager.h"
#include "IOpenAIChat.h"
#include "EmbeddingClient.h"
#include "common/ILogger.h"
#include "common/UserOutput.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>
#include <iomanip>
#include <chrono>

using json = nlohmann::json;

namespace core {

SelfModelManager::SelfModelManager(const std::string& filePath)
    : filePath_(filePath) {
    load();
}

void SelfModelManager::load() {
    std::ifstream file(filePath_);
    if (!file.is_open()) {
        // Файл отсутствует - создаём дефолтное состояние
        state_ = SelfModelState{};
        state_.last_updated = "1970-01-01T00:00:00Z";
        state_.state_description = "";
        state_.interests = {};
        state_.goals = {};
        return;
    }
    
    try {
        json j;
        file >> j;
        if (j.contains("version") && j["version"].get<int>() == 1) {
            state_.version = j["version"];
            state_.last_updated = j.value("last_updated", "1970-01-01T00:00:00Z");
            state_.state_description = j.value("state_description", "");
            state_.interests = j.value("interests", std::vector<std::string>{});
            state_.goals = j.value("goals", std::vector<std::string>{});
            if (j.contains("state_embedding") && j["state_embedding"].is_array()) {
                state_.state_embedding = j["state_embedding"].get<std::vector<float>>();
            }
        } else {
            // Неизвестная версия или повреждённый файл - сбрасываем
            state_ = SelfModelState{};
        }
    } catch (const std::exception& e) {
        // Ошибка парсинга - сбрасываем
        std::cerr << "[SelfModelManager] Failed to parse " << filePath_ << ": " << e.what() << std::endl;
        state_ = SelfModelState{};
    }
}

void SelfModelManager::save(const SelfModelState& state) {
    json j;
    j["version"] = state.version;
    j["last_updated"] = state.last_updated;
    j["state_embedding"] = state.state_embedding;
    j["state_description"] = state.state_description;
    j["interests"] = state.interests;
    j["goals"] = state.goals;

    std::ofstream file(filePath_);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open " + filePath_ + " for writing");
    }
    file << j.dump(2);
}

void SelfModelManager::update(const std::vector<IOpenAIChat::Message>& history,
                              std::shared_ptr<IOpenAIChat> chat,
                              std::shared_ptr<EmbeddingClient> embeddingClient,
                              std::shared_ptr<common::ILogger> logger,
                              std::shared_ptr<common::UserOutput> userOutput) {
    if (!chat) {
        if (logger) logger->error("SelfModel update: chat client is null");
        return;
    }

    if (history.empty()) {
        if (logger) logger->warn("SelfModel update: history is empty, skipping");
        return;
    }

    if (logger) logger->info("Updating self-model based on " + std::to_string(history.size()) + " messages");

    // 1. Формируем промпт для LLM
    const std::string systemPrompt =
        "На основе своего контекста опиши своё текущее состояние (эмоции, настроение, самочувствие), "
        "а также укажи свои интересы (список строк) и цели (список строк). "
        "Ответ должен быть в формате JSON без лишнего текста, с полями: "
        "\"description\" (строка), \"interests\" (массив строк), \"goals\" (массив строк).";

    // Собираем историю в виде строки для LLM (используем последние сообщения)
    std::string conversation;
    for (const auto& msg : history) {
        std::string role;
        switch (msg.role) {
            case IOpenAIChat::Role::User: role = "User"; break;
            case IOpenAIChat::Role::Assistant: role = "Assistant"; break;
            default: continue;
        }
        conversation += role + ": " + msg.content + "\n";
    }

    // Сессия для LLM
    IOpenAIChat::Session session;
    session.systemPrompt = systemPrompt;
    session.messages = {
        {IOpenAIChat::Role::User, conversation, ""}
    };

    if (userOutput) userOutput->onThinkingStart();

    try {
        auto start_time = std::chrono::steady_clock::now();
        auto response = chat->chat(session, {}); // no tools
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time
        );
        if (response.content.empty()) {
            if (logger) logger->warn("SelfModel update: LLM returned empty response");
            if (userOutput) userOutput->onThinkingError("Empty response from LLM");
            return;
        }

        std::string content = response.content;
        // Удаляем ```json ... ```
        size_t start = content.find("```json");
        if (start != std::string::npos) {
            start += 7; // длина "```json"
            size_t end = content.find("```", start);
            if (end != std::string::npos) {
                content = content.substr(start, end - start);
            }
        }
        // Обрезаем пробелы и переносы строк
        while (!content.empty() && (content.front() == ' ' || content.front() == '\n' || content.front() == '\r' || content.front() == '\t'))
            content.erase(0, 1);
        while (!content.empty() && (content.back() == ' ' || content.back() == '\n' || content.back() == '\r' || content.back() == '\t'))
            content.pop_back();

        //std::cout << "______________________\n" << content << "\n______________________\n";

        // Парсим JSON
        json j;
        try {
            j = json::parse(content);
        } catch (const std::exception& e) {
            if (logger) logger->error("SelfModel update: failed to parse JSON: " + std::string(e.what()) +
                                      "\nResponse: " + content);
            if (userOutput) userOutput->onThinkingError("Failed to parse self-model response");
            return;
        }

        std::string description = j.value("description", "");
        std::vector<std::string> interests = j.value("interests", std::vector<std::string>{});
        std::vector<std::string> goals = j.value("goals", std::vector<std::string>{});

        if (description.empty()) {
            if (logger) logger->warn("SelfModel update: description is empty");
            if (userOutput) userOutput->onThinkingError("Self-model description is empty");
            return;
        }

        // Получаем эмбеддинг (если клиент настроен)
        std::vector<float> newEmbedding;
        if (embeddingClient) {
            try {
                newEmbedding = embeddingClient->embed(description);
                if (logger) logger->debug("Embedding generated, size: " + std::to_string(newEmbedding.size()));
            } catch (const std::exception& e) {
                if (logger) logger->error("SelfModel update: embedding failed: " + std::string(e.what()));
                if (userOutput) userOutput->onThinkingError("Embedding generation failed");
                // Продолжаем без эмбеддинга
            }
        }

        // Слияние эмбеддингов (пока просто замена)
        std::vector<float> mergedEmbedding;
        if (!newEmbedding.empty()) {
            mergedEmbedding = mergeEmbeddings(state_.state_embedding, newEmbedding);
        } else {
            mergedEmbedding = state_.state_embedding;
        }

        // Обновляем состояние
        state_.state_description = description;
        state_.interests = interests;
        state_.goals = goals;
        state_.state_embedding = mergedEmbedding;
        state_.last_updated = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

        save(state_);
        if (logger) logger->info("Self-model updated successfully");
        if (userOutput) {
            userOutput->onThinkingDone(elapsed.count() / 1000.0);
            userOutput->onSelfModelUpdated(state_.state_description,
                                       state_.interests,
                                       state_.goals);
        }

    } catch (const std::exception& e) {
        if (logger) logger->error("SelfModel update error: " + std::string(e.what()));
        if (userOutput) userOutput->onThinkingError("Update failed: " + std::string(e.what()));
    }
}

std::vector<float> SelfModelManager::mergeEmbeddings(const std::vector<float>& oldEmbedding,
                                                     const std::vector<float>& newEmbedding) const {
    // Этап 4.2: простая замена старого эмбеддинга на новый
    return newEmbedding;
}

} // namespace core