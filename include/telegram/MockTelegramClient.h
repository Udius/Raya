// telegram/MockTelegramClient.h
#pragma once

#include "telegram/ITelegramClient.h"
#include "telegram/Message.h"
#include "telegram/ChatInfo.h"
#include "telegram/TdConfig.h"
#include "telegram/RetryPolicy.h"

#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <future>
#include <chrono>
#include <string>
#include <cstdint>
#include <algorithm>

namespace telegram {

/**
 * @brief Мок-реализация Telegram-клиента для юнит-тестов.
 * 
 * Эмулирует авторизацию, отправку/редактирование/удаление сообщений,
 * подписку на новые сообщения и доставку. Хранит отправленные сообщения
 * для верификации в тестах.
 */
class MockTelegramClient : public ITelegramClient {
public:
    MockTelegramClient();

    // ----- ITelegramClient -----
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

    // ----- Дополнительные методы для тестов -----
    /// Симулировать входящее сообщение (вызывает все зарегистрированные колбэки)
    void simulateIncomingMessage(int64_t chatId, int64_t messageId, const std::string& text);

    /// Симулировать статус доставки для сообщения
    void simulateDeliveryStatus(int64_t messageId, MessageStatus status);

    /// Установить ожидаемый код подтверждения (для успешной авторизации)
    void setExpectedCode(const std::string& code);

    /// Установить ожидаемый пароль (для двухфакторной аутентификации)
    void setExpectedPassword(const std::string& password);

    /// Принудительно установить состояние авторизации (для тестирования переходов)
    void setAuthState(AuthState state);

    /// Получить список отправленных сообщений (пара: chatId, текст)
    const std::vector<std::pair<int64_t, std::string>>& getSentMessages() const;

    /// Очистить список отправленных сообщений
    void clearSentMessages();

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

    // Хранилище сообщений по чатам (для эмуляции)
    std::unordered_map<int64_t, std::vector<Message>> messages_;
    std::atomic<int64_t> nextMessageId_{1000};

    RetryPolicy policy_; // не используется, но храним

    // Для проверки отправленных сообщений в тестах
    mutable std::mutex sentMutex_;
    std::vector<std::pair<int64_t, std::string>> sentMessages_;
};

} // namespace telegram