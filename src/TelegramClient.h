// src/TelegramClient.h
#pragma once

#include "telegram/ITelegramClient.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <thread>
#include <memory>
#include <condition_variable>
#include <queue>

// Forward declaration для TDLib (в реальности включите td/telegram/Client.h)
namespace td { class ClientManager; }

namespace telegram {

class TelegramClient : public ITelegramClient {
public:
    explicit TelegramClient(const RetryPolicy& defaultPolicy = RetryPolicy{});
    ~TelegramClient();

    // ITelegramClient
    AFuture<void> connect(const TdConfig& config) override;
    AFuture<void> disconnect() override;
    AFuture<void> logout() override;

    AFuture<void> login(const std::string& phoneNumber) override;
    AFuture<void> setAuthCode(const std::string& code) override;
    AFuture<void> setPassword(const std::string& password) override;

    AFuture<MessageResult> sendMessage(int64_t chatId, const std::string& text) override;
    AFuture<void> editMessage(int64_t chatId, int64_t messageId, const std::string& newText) override;
    AFuture<void> deleteMessage(int64_t chatId, int64_t messageId) override;

    AFuture<std::vector<ChatInfo>> getChats() override;

    std::uint64_t addMessageListener(MessageCallback callback) override;
    void removeMessageListener(std::uint64_t listenerId) override;
    void onMessageDelivered(DeliveryCallback callback) override;
    void onAuthStateChanged(AuthStateCallback callback) override;

    AuthState getAuthState() const override;
    void setRetryPolicy(const RetryPolicy& policy) override;

private:
    // Вспомогательные методы для работы с TDLib
    void initTdLib();
    void shutdownTdLib();
    void runReceiveLoop(); // поток для td::ClientManager::receive()

    template<typename T>
    AFuture<T> sendRequestAndWait(const std::string& requestJson);

    // Обёртка с политикой повторных попыток
    template<typename T>
    AFuture<T> executeWithRetry(std::function<AFuture<T>()> operation);

    // Обработчик входящих обновлений
    void processUpdate(const std::string& updateJson);

    

private:
    struct ListenerEntry {
        std::uint64_t id;
        MessageCallback callback;
    };

    TdConfig config_;
    RetryPolicy retryPolicy_;
    std::atomic<AuthState> authState_{AuthState::LoggedOut};
    std::atomic<bool> running_{false};

    std::unique_ptr<td::ClientManager> clientManager_; // реальный объект TDLib
    std::thread receiverThread_;
    std::mutex receiveMutex_;
    std::condition_variable receiveCv_;
    std::queue<std::string> pendingUpdates_; // для асинхронной обработки

    std::mutex listenerMutex_;
    std::vector<ListenerEntry> messageListeners_;
    std::uint64_t nextListenerId_{1};

    std::mutex deliveryMutex_;
    DeliveryCallback deliveryCallback_;

    std::mutex authCallbackMutex_;
    AuthStateCallback authStateCallback_;

    // Отображение запросов (promise) по ID, если нужно
    // std::unordered_map<std::uint64_t, std::shared_ptr<void>> pendingRequests_;
};

} // namespace telegram