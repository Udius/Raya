#include "EmbeddingClient.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <iostream>
#include <sstream>

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

EmbeddingClient::EmbeddingClient(const Endpoint& endpoint)
    : endpoint_(endpoint) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

EmbeddingClient::~EmbeddingClient() {
    curl_global_cleanup();
}

std::vector<float> EmbeddingClient::embed(const std::string& text) {
    if (endpoint_.baseUrl.empty() || endpoint_.model.empty()) {
        throw std::runtime_error("Embedding endpoint or model not configured");
    }

    json requestBody;
    requestBody["model"] = endpoint_.model;
    requestBody["input"] = text;

    std::string body = requestBody.dump();
    std::string url = endpoint_.baseUrl + "/embeddings";

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::string responseString;
    std::string authHeader = "Authorization: Bearer " + endpoint_.bearerKey;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, authHeader.c_str());
    headers = curl_slist_append(headers, "User-Agent: Raya-AI-Agent/1.0");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseString);
    // Для отладки: curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        throw std::runtime_error("CURL error: " + std::string(curl_easy_strerror(res)));
    }
    if (http_code != 200) {
        throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + responseString);
    }

    json response = json::parse(responseString);
    if (!response.contains("data") || response["data"].empty()) {
        throw std::runtime_error("No embedding data in response");
    }
    auto embedding = response["data"][0]["embedding"];
    if (!embedding.is_array()) {
        throw std::runtime_error("Embedding is not an array");
    }
    return embedding.get<std::vector<float>>();
}