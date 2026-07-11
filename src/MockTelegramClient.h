// src/MockTelegramClient.h
#pragma once

#include "telegram/ITelegramClient.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <future>

namespace telegram {

class MockTelegramClient : public ITelegramClient {
public:
    MockTelegramClient();

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

    // Специальные методы для тестов
    void simulateIncomingMessage(int64_t chatId, int64_t messageId, const std::string& text);
    void simulateDeliveryStatus(int64_t messageId, MessageStatus status);
    void setAuthState(AuthState state);
    void setExpectedCode(const std::string& code);
    void setExpectedPassword(const std::string& password);

private:
    struct ListenerEntry {
        std::uint64_t id;
        MessageCallback callback;
    };

    std::atomic<AuthState> authState_{AuthState::LoggedOut};
    std::string expectedCode_;
    std::string expectedPassword_;

    std::mutex listenerMutex_;
    std::vector<ListenerEntry> messageListeners_;
    std::uint64_t nextListenerId_{1};

    std::mutex deliveryMutex_;
    DeliveryCallback deliveryCallback_;

    std::mutex authCallbackMutex_;
    AuthStateCallback authStateCallback_;

    std::unordered_map<int64_t, std::vector<Message>> messages_; // chatId -> список сообщений
    std::atomic<int64_t> nextMessageId_{1000};

    RetryPolicy policy_; // не используется, но храним
};

} // namespace telegram