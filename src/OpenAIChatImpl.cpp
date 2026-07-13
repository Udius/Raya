// src/OpenAIChatImpl.cpp
#include "OpenAIChatImpl.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <chrono>

using json = nlohmann::json;

// Callback для записи ответа
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

OpenAIChatImpl::OpenAIChatImpl(const Endpoint& endpoint, const std::string& model)
    : endpoint_(endpoint), model_(model) {}

OpenAIChatImpl::~OpenAIChatImpl() {
    // Ничего не нужно, так как curl инициализируется глобально
}

std::string OpenAIChatImpl::chat(const Session& session) {
    // Подготовка JSON
    json requestBody;
    requestBody["model"] = model_;
    requestBody["messages"] = json::array();

    if (!session.systemPrompt.empty()) {
        json systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = session.systemPrompt;
        requestBody["messages"].push_back(systemMsg);
    }

    for (const auto& msg : session.messages) {
        json msgJson;
        std::string roleStr;
        switch (msg.role) {
            case Role::System: roleStr = "system"; break;
            case Role::User: roleStr = "user"; break;
            case Role::Assistant: roleStr = "assistant"; break;
            default: continue;
        }
        msgJson["role"] = roleStr;
        msgJson["content"] = msg.content;
        requestBody["messages"].push_back(msgJson);
    }

    requestBody["temperature"] = 0.7;
    requestBody["max_tokens"] = 1000;

    std::string body = requestBody.dump();

    // URL для запроса
    std::string url = endpoint_.baseUrl + "/chat/completions";

    // Инициализация curl
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::string responseString;
    std::string authHeader = "Authorization: Bearer " + endpoint_.bearerKey;

    // Настройки
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());

    // Заголовки
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "User-Agent: Raya-AI-Agent/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Таймауты
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    // SSL (проверка сертификата включена по умолчанию)
    // Можно отключить для теста: curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    // Callback для записи ответа
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseString);

    // Выполнение запроса
    CURLcode res = curl_easy_perform(curl);

    // Обработка ошибок
    if (res != CURLE_OK) {
        std::string errorMsg = "curl error: " + std::string(curl_easy_strerror(res));
        std::cerr << "[OpenAIChat] " << errorMsg << std::endl;
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        throw std::runtime_error(errorMsg);
    }

    // Получение HTTP-кода
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    // Проверка статуса
    if (http_code != 200) {
        std::string errorMsg = "API returned status " + std::to_string(http_code) +
                               ": " + responseString;
        std::cerr << "[OpenAIChat] " << errorMsg << std::endl;
        throw std::runtime_error(errorMsg);
    }

    // Парсинг ответа
    try {
        json response = json::parse(responseString);
        auto choices = response["choices"];
        if (choices.empty()) {
            throw std::runtime_error("No choices in response");
        }
        auto message = choices[0]["message"];
        std::string content = message["content"];
        return content;
    } catch (const std::exception& e) {
        std::cerr << "[OpenAIChat] Parse error: " << e.what() << std::endl;
        throw std::runtime_error("Failed to parse response: " + std::string(e.what()));
    }
}