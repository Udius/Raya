// include/telegram/ITelegramClient.h
#pragma once

#include <functional>
#include <future>
#include <string>
#include <vector>
#include <cstdint>
#include "Message.h"
#include "AuthState.h"
#include "ChatInfo.h"
#include "TdConfig.h"
#include "RetryPolicy.h"

namespace telegram {

template<typename T>
using AFuture = std::future<T>; // заглушка, может быть заменена на свой тип с корутинами

/**
 * @brief Интерфейс Telegram-клиента с полной функциональностью.
 */
class ITelegramClient {
public:
    virtual ~ITelegramClient() = default;

    // ----- Управление подключением -----
    virtual AFuture<void> connect(const TdConfig& config) = 0;
    virtual AFuture<void> disconnect() = 0;
    virtual AFuture<void> logout() = 0;

    // ----- Авторизация -----
    virtual AFuture<void> login(const std::string& phoneNumber) = 0;
    virtual AFuture<void> setAuthCode(const std::string& code) = 0;
    virtual AFuture<void> setPassword(const std::string& password) = 0;

    // ----- Отправка и управление сообщениями -----
    virtual AFuture<MessageResult> sendMessage(int64_t chatId, const std::string& text) = 0;
    virtual AFuture<void> editMessage(int64_t chatId, int64_t messageId, const std::string& newText) = 0;
    virtual AFuture<void> deleteMessage(int64_t chatId, int64_t messageId) = 0;

    // ----- Получение данных -----
    virtual AFuture<std::vector<ChatInfo>> getChats() = 0;

    // ----- События -----
    using MessageCallback = std::function<void(int64_t chatId, int64_t messageId, const std::string& text)>;
    using DeliveryCallback = std::function<void(int64_t messageId, MessageStatus status)>;
    using AuthStateCallback = std::function<void(AuthState newState)>;

    virtual std::uint64_t addMessageListener(MessageCallback callback) = 0;
    virtual void removeMessageListener(std::uint64_t listenerId) = 0;
    virtual void onMessageDelivered(DeliveryCallback callback) = 0;
    virtual void onAuthStateChanged(AuthStateCallback callback) = 0;

    // ----- Состояние -----
    virtual AuthState getAuthState() const = 0;

    // ----- Настройка политики повторных попыток -----
    virtual void setRetryPolicy(const RetryPolicy& policy) = 0;
};

} // namespace telegram