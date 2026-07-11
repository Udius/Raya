// src/MockTelegramClient.cpp
#include "telegram/MockTelegramClient.h"
#include <stdexcept>
#include <future>
#include <chrono>
#include <algorithm>

namespace telegram {

MockTelegramClient::MockTelegramClient() = default;

AFuture<void> MockTelegramClient::connect(const TdConfig& /*config*/) {
    std::promise<void> p;
    p.set_value();
    return p.get_future();
}

AFuture<void> MockTelegramClient::disconnect() {
    authState_ = AuthState::LoggedOut;
    std::promise<void> p;
    p.set_value();
    return p.get_future();
}

AFuture<void> MockTelegramClient::logout() {
    authState_ = AuthState::LoggedOut;
    std::promise<void> p;
    p.set_value();
    return p.get_future();
}

AFuture<void> MockTelegramClient::login(const std::string& /*phoneNumber*/) {
    if (authState_ != AuthState::LoggedOut) {
        throw std::logic_error("Already logging in");
    }
    authState_ = AuthState::WaitingForCode;
    std::promise<void> p;
    p.set_value();
    return p.get_future();
}

AFuture<void> MockTelegramClient::setAuthCode(const std::string& code) {
    if (authState_ != AuthState::WaitingForCode) {
        throw std::logic_error("Not waiting for code");
    }
    if (code == expectedCode_) {
        authState_ = AuthState::LoggedIn;
    } else {
        authState_ = AuthState::Error;
        throw std::runtime_error("Invalid code");
    }
    std::promise<void> p;
    p.set_value();
    return p.get_future();
}

AFuture<void> MockTelegramClient::setPassword(const std::string& password) {
    if (authState_ != AuthState::WaitingForPassword) {
        throw std::logic_error("Not waiting for password");
    }
    if (password == expectedPassword_) {
        authState_ = AuthState::LoggedIn;
    } else {
        authState_ = AuthState::Error;
        throw std::runtime_error("Invalid password");
    }
    std::promise<void> p;
    p.set_value();
    return p.get_future();
}

AFuture<MessageResult> MockTelegramClient::sendMessage(int64_t chatId, const std::string& text) {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }
    if (chatId == 0) {
        throw std::invalid_argument("chatId cannot be 0");
    }
    sentMessages_.push_back({chatId, text});
    int64_t newId = nextMessageId_++;
    Message msg{newId, chatId, text, std::chrono::system_clock::now(), MessageStatus::Sent};
    messages_[chatId].push_back(msg);
    MessageResult result{newId, MessageStatus::Sent};
    // Если установлен колбэк доставки, вызываем его
    {
        std::lock_guard lock(deliveryMutex_);
        if (deliveryCallback_) {
            deliveryCallback_(newId, MessageStatus::Sent);
        }
    }
    std::promise<MessageResult> p;
    p.set_value(result);
    return p.get_future();
}

AFuture<void> MockTelegramClient::editMessage(int64_t chatId, int64_t messageId, const std::string& newText) {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }
    auto it = messages_.find(chatId);
    if (it == messages_.end()) {
        throw std::runtime_error("Chat not found");
    }
    for (auto& m : it->second) {
        if (m.id == messageId) {
            m.text = newText;
            std::promise<void> p;
            p.set_value();
            return p.get_future();
        }
    }
    throw std::runtime_error("Message not found");
}

AFuture<void> MockTelegramClient::deleteMessage(int64_t chatId, int64_t messageId) {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }
    auto it = messages_.find(chatId);
    if (it != messages_.end()) {
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [messageId](const Message& m) { return m.id == messageId; }),
                  vec.end());
    }
    std::promise<void> p;
    p.set_value();
    return p.get_future();
}

AFuture<std::vector<ChatInfo>> MockTelegramClient::getChats() {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }
    std::promise<std::vector<ChatInfo>> p;
    p.set_value({{100, "Mock Chat 1", false}, {101, "Mock Group", true}});
    return p.get_future();
}

std::uint64_t MockTelegramClient::addMessageListener(MessageCallback callback) {
    std::lock_guard lock(listenerMutex_);
    std::uint64_t id = nextListenerId_++;
    messageListeners_.push_back({id, std::move(callback)});
    return id;
}

void MockTelegramClient::removeMessageListener(std::uint64_t listenerId) {
    std::lock_guard lock(listenerMutex_);
    auto it = std::find_if(messageListeners_.begin(), messageListeners_.end(),
                           [listenerId](const ListenerEntry& e) { return e.id == listenerId; });
    if (it != messageListeners_.end()) {
        messageListeners_.erase(it);
    }
}

void MockTelegramClient::onMessageDelivered(DeliveryCallback callback) {
    std::lock_guard lock(deliveryMutex_);
    deliveryCallback_ = std::move(callback);
}

void MockTelegramClient::onAuthStateChanged(AuthStateCallback callback) {
    std::lock_guard lock(authCallbackMutex_);
    authStateCallback_ = std::move(callback);
}

AuthState MockTelegramClient::getAuthState() const {
    return authState_.load();
}

void MockTelegramClient::setRetryPolicy(const RetryPolicy& policy) {
    policy_ = policy;
}

// ----- Специальные методы для тестов -----
void MockTelegramClient::simulateIncomingMessage(int64_t chatId, int64_t messageId, const std::string& text) {
    std::lock_guard lock(listenerMutex_);
    for (auto& listener : messageListeners_) {
        listener.callback(chatId, messageId, text);
    }
}

void MockTelegramClient::simulateDeliveryStatus(int64_t messageId, MessageStatus status) {
    std::lock_guard lock(deliveryMutex_);
    if (deliveryCallback_) {
        deliveryCallback_(messageId, status);
    }
}

void MockTelegramClient::setAuthState(AuthState state) {
    authState_ = state;
    std::lock_guard lock(authCallbackMutex_);
    if (authStateCallback_) {
        authStateCallback_(state);
    }
}

void MockTelegramClient::setExpectedCode(const std::string& code) {
    expectedCode_ = code;
}

void MockTelegramClient::setExpectedPassword(const std::string& password) {
    expectedPassword_ = password;
}

const std::vector<std::pair<int64_t, std::string>>& MockTelegramClient::getSentMessages() const {
    return sentMessages_;
}

} // namespace telegram