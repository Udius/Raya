#pragma once

#include <string>
#include <vector>

class EmbeddingClient {
public:
    struct Endpoint {
        std::string baseUrl;
        std::string bearerKey;
        std::string model;
    };

    EmbeddingClient(const Endpoint& endpoint);
    ~EmbeddingClient();

    /// Возвращает вектор эмбеддинга для переданного текста
    std::vector<float> embed(const std::string& text);

private:
    Endpoint endpoint_;
};