// src/TelegramClient.cpp
#include "TelegramClient.h"
#include <stdexcept>
#include <thread>
#include <chrono>
#include <future>
#include <iostream>
#include <random>
#include <algorithm>

// Заглушка для TDLib (в реальности подключите td/telegram/Client.h)
namespace td {
class ClientManager {
public:
    void send(std::uint64_t id, const std::string& request) {}
    std::string receive(double timeout) { return ""; }
    void close() {}
};
} // namespace td

namespace telegram {

TelegramClient::TelegramClient(const RetryPolicy& defaultPolicy)
    : retryPolicy_(defaultPolicy) {}

TelegramClient::~TelegramClient() {
    disconnect().wait();
}

// ----- Подключение / отключение -----
AFuture<void> TelegramClient::connect(const TdConfig& config) {
    if (running_.load()) {
        throw std::logic_error("Already connected");
    }
    config_ = config;
    initTdLib();
    running_ = true;
    receiverThread_ = std::thread(&TelegramClient::runReceiveLoop, this);
    std::promise<void> promise;
    promise.set_value();
    return promise.get_future();
}

AFuture<void> TelegramClient::disconnect() {
    if (!running_.load()) {
        return std::async(std::launch::async, [](){});
    }
    running_ = false;
    receiveCv_.notify_all();
    if (receiverThread_.joinable()) {
        receiverThread_.join();
    }
    shutdownTdLib();
    authState_ = AuthState::LoggedOut;
    std::promise<void> promise;
    promise.set_value();
    return promise.get_future();
}

AFuture<void> TelegramClient::logout() {
    if (authState_ == AuthState::LoggedOut) {
        std::promise<void> promise;
        promise.set_value();
        return promise.get_future();
    }
    // TDLib: send request "logOut"
    // После выхода состояние станет LoggedOut
    authState_ = AuthState::LoggedOut;
    std::promise<void> promise;
    promise.set_value();
    return promise.get_future();
}

// ----- Инициализация TDLib (реальная) -----
void TelegramClient::initTdLib() {
    // Создаём ClientManager, устанавливаем параметры:
    // api_id, api_hash, database_directory и т.д.
    clientManager_ = std::make_unique<td::ClientManager>();
    // Настройка параметров через td::td_api::setTdlibParameters
    // ...
    std::cout << "[TelegramClient] TDLib initialized with config: api_id=" 
              << config_.api_id << ", db=" << config_.database_directory << std::endl;
}

void TelegramClient::shutdownTdLib() {
    if (clientManager_) {
        clientManager_->close();
        clientManager_.reset();
    }
}

// ----- Поток для receive() -----
void TelegramClient::runReceiveLoop() {
    while (running_.load()) {
        auto response = clientManager_->receive(1.0); // timeout 1 sec
        if (!response.empty()) {
            processUpdate(response);
        }
    }
}

// ----- Обработка обновлений (заглушка) -----
void TelegramClient::processUpdate(const std::string& updateJson) {
    // Здесь парсим JSON и вызываем соответствующие колбэки
    // Например, если пришло updateNewMessage, вызываем messageListeners
    // Если пришло updateMessageSendSucceeded – вызываем deliveryCallback_
    // Если пришло updateAuthorizationState – обновляем authState_ и вызываем authCallback_
    // Для примера – ничего не делаем.
}

// ----- Авторизация -----
AFuture<void> TelegramClient::login(const std::string& phoneNumber) {
    if (authState_ != AuthState::LoggedOut) {
        throw std::logic_error("Already logging in");
    }
    // Отправляем setAuthenticationPhoneNumber
    authState_ = AuthState::WaitingForCode;
    std::promise<void> promise;
    promise.set_value();
    return promise.get_future();
}

AFuture<void> TelegramClient::setAuthCode(const std::string& code) {
    if (authState_ != AuthState::WaitingForCode) {
        throw std::logic_error("Not waiting for code");
    }
    // Отправляем checkAuthenticationCode
    // Если код неверен -> бросаем исключение, состояние -> Error
    try {
        // Здесь может прийти ответ с ошибкой
        authState_ = AuthState::LoggedIn; // или WaitingForPassword
    } catch (...) {
        authState_ = AuthState::Error;
        throw;
    }
    std::promise<void> promise;
    promise.set_value();
    return promise.get_future();
}

AFuture<void> TelegramClient::setPassword(const std::string& password) {
    if (authState_ != AuthState::WaitingForPassword) {
        throw std::logic_error("Not waiting for password");
    }
    try {
        // checkAuthenticationPassword
        authState_ = AuthState::LoggedIn;
    } catch (...) {
        authState_ = AuthState::Error;
        throw;
    }
    std::promise<void> promise;
    promise.set_value();
    return promise.get_future();
}

// ----- Отправка сообщений (с retry) -----
AFuture<MessageResult> TelegramClient::sendMessage(int64_t chatId, const std::string& text) {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }

    auto operation = [this, chatId, text]() -> AFuture<MessageResult> {
        // Здесь реальный запрос к TDLib sendMessage
        // Возвращаем future с результатом
        std::promise<MessageResult> promise;
        // Имитация
        MessageResult result{12345 + chatId, MessageStatus::Sent};
        promise.set_value(result);
        return promise.get_future();
    };

    return executeWithRetry<MessageResult>(operation);
}

AFuture<void> TelegramClient::editMessage(int64_t chatId, int64_t messageId, const std::string& newText) {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }
    auto operation = [this, chatId, messageId, newText]() -> AFuture<void> {
        // editMessageText
        std::promise<void> promise;
        promise.set_value();
        return promise.get_future();
    };
    return executeWithRetry<void>(operation);
}

AFuture<void> TelegramClient::deleteMessage(int64_t chatId, int64_t messageId) {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }
    auto operation = [this, chatId, messageId]() -> AFuture<void> {
        // deleteMessages
        std::promise<void> promise;
        promise.set_value();
        return promise.get_future();
    };
    return executeWithRetry<void>(operation);
}

// ----- Получение списка чатов -----
AFuture<std::vector<ChatInfo>> TelegramClient::getChats() {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }
    // Запрос getChats с лимитом, возвращаем список
    std::promise<std::vector<ChatInfo>> promise;
    promise.set_value({{123, "Test Chat", false}}); // заглушка
    return promise.get_future();
}

// ----- Подписка на события -----
std::uint64_t TelegramClient::addMessageListener(MessageCallback callback) {
    std::lock_guard lock(listenerMutex_);
    std::uint64_t id = nextListenerId_++;
    messageListeners_.push_back({id, std::move(callback)});
    return id;
}

void TelegramClient::removeMessageListener(std::uint64_t listenerId) {
    std::lock_guard lock(listenerMutex_);
    auto it = std::find_if(messageListeners_.begin(), messageListeners_.end(),
                           [listenerId](const ListenerEntry& e) { return e.id == listenerId; });
    if (it != messageListeners_.end()) {
        messageListeners_.erase(it);
    }
}

void TelegramClient::onMessageDelivered(DeliveryCallback callback) {
    std::lock_guard lock(deliveryMutex_);
    deliveryCallback_ = std::move(callback);
}

void TelegramClient::onAuthStateChanged(AuthStateCallback callback) {
    std::lock_guard lock(authCallbackMutex_);
    authStateCallback_ = std::move(callback);
}

AuthState TelegramClient::getAuthState() const {
    return authState_.load();
}

void TelegramClient::setRetryPolicy(const RetryPolicy& policy) {
    retryPolicy_ = policy;
}

// ----- Шаблон executeWithRetry (реализация) -----
template<typename T>
AFuture<T> TelegramClient::executeWithRetry(std::function<AFuture<T>()> operation) {
    // Возвращаем future, который внутри делает попытки с задержками.
    // Для простоты используем std::async, в реальности лучше использовать свой механизм.
    return std::async(std::launch::async, [this, operation]() -> T {
        int attempts = 0;
        std::chrono::milliseconds delay = retryPolicy_.initial_delay;
        while (true) {
            try {
                auto future = operation();
                return future.get();
            } catch (const std::exception& e) {
                attempts++;
                if (attempts >= retryPolicy_.max_attempts) {
                    throw; // перебрасываем последнее исключение
                }
                // Проверяем, является ли ошибка временной (по коду)
                // Для примера считаем все ошибки временными
                std::this_thread::sleep_for(delay);
                if (retryPolicy_.exponential) {
                    delay = std::chrono::milliseconds(
                        static_cast<int>(delay.count() * retryPolicy_.backoff_multiplier)
                    );
                }
            }
        }
    });
}

// Явные инстанцирования для используемых типов
template AFuture<void> TelegramClient::executeWithRetry(std::function<AFuture<void>()>);
template AFuture<MessageResult> TelegramClient::executeWithRetry(std::function<AFuture<MessageResult>()>);

} // namespace telegram