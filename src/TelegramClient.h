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
#include <future>
#include <unordered_set>

// TDLib
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

namespace telegram {

class TelegramClient : public ITelegramClient {
public:
    explicit TelegramClient(const RetryPolicy& defaultPolicy = RetryPolicy{});
    ~TelegramClient() override;

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
    using RequestId = std::uint64_t;
    using PromisePtr = std::shared_ptr<std::promise<std::unique_ptr<td::td_api::Object>>>;

    // ---- Отправка запроса с использованием object_ptr (td::td_api::object_ptr) ----
    AFuture<std::unique_ptr<td::td_api::Object>> sendQuery(
        td::td_api::object_ptr<td::td_api::Function> function);

    // Шаблонная обёртка – теперь принимает object_ptr, а не std::unique_ptr
    template<typename ResponseT>
    AFuture<ResponseT> sendQueryTyped(td::td_api::object_ptr<td::td_api::Function> function) {
        auto futureObj = sendQuery(std::move(function));
        return std::async(std::launch::async, [future = std::move(futureObj)]() mutable -> ResponseT {
            auto raw = future.get();    // mutable позволяет вызвать get() на неконстантном future
            if (!raw) {
                throw std::runtime_error("Empty response");
            }
            if (raw->get_id() == td::td_api::error::ID) {
                auto* err = static_cast<td::td_api::error*>(raw.get());
                throw std::runtime_error("TDLib error: " + err->message_ +
                                        " (code " + std::to_string(err->code_) + ")");
            }
            if (raw->get_id() != ResponseT::ID) {
                throw std::runtime_error("Unexpected response type");
            }
            return std::move(static_cast<ResponseT&>(*raw));
        });
    }

    // ---- Шаблон executeWithRetry (объявлен, определён ниже в .cpp) ----
    template<typename T>
    AFuture<T> executeWithRetry(std::function<AFuture<T>()> operation);

    void initTdLib(const TdConfig& config);
    void shutdownTdLib();
    void runReceiveLoop();
    void processResponse(td::ClientManager::Response response);
    void processUpdate(td::td_api::object_ptr<td::td_api::Object> update);
    void setAuthState(AuthState newState);

    AFuture<MessageResult> sendMessageImpl(int64_t chatId, const std::string& text);

private:
    struct ListenerEntry {
        std::uint64_t id;
        MessageCallback callback;
    };

    TdConfig config_;
    RetryPolicy retryPolicy_;
    std::atomic<AuthState> authState_{AuthState::LoggedOut};
    std::atomic<bool> running_{false};

    std::unique_ptr<td::ClientManager> clientManager_;
    td::ClientManager::ClientId clientId_{0};
    std::thread receiverThread_;

    std::mutex pendingMutex_;
    std::unordered_map<RequestId, PromisePtr> pendingRequests_;
    std::atomic<RequestId> nextRequestId_{1};

    std::mutex listenerMutex_;
    std::vector<ListenerEntry> messageListeners_;
    std::uint64_t nextListenerId_{1};

    std::mutex deliveryMutex_;
    DeliveryCallback deliveryCallback_;
    std::mutex authCallbackMutex_;
    AuthStateCallback authStateCallback_;
};

} // namespace telegram