// src/TelegramClient.cpp
#include "TelegramClient.h"
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <stdexcept>
#include <thread>
#include <chrono>
#include <future>
#include <iostream>
#include <algorithm>
#include <utility>

namespace telegram {

// ------------------------------------------------------------------
// Конструктор / Деструктор
// ------------------------------------------------------------------
TelegramClient::TelegramClient(const RetryPolicy& defaultPolicy)
    : retryPolicy_(defaultPolicy) {}

TelegramClient::~TelegramClient() {
    disconnect().wait();
}

// ------------------------------------------------------------------
// Подключение / отключение
// ------------------------------------------------------------------
AFuture<void> TelegramClient::connect(const TdConfig& config) {
    if (running_.load()) {
        throw std::logic_error("Already connected");
    }
    config_ = config;
    initTdLib(config);
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
        std::promise<void> p;
        p.set_value();
        return p.get_future();
    }
    auto request = td::td_api::make_object<td::td_api::logOut>();
    auto future = sendQueryTyped<td::td_api::ok>(std::move(request));
    return std::async(std::launch::async, [future = std::move(future)]() mutable {
        future.get(); // ждём результат
        // Состояние обновится через updateAuthorizationState
    });
}

// ------------------------------------------------------------------
// Инициализация TDLib
// ------------------------------------------------------------------
void TelegramClient::initTdLib(const TdConfig& config) {
    td::ClientManager::execute(td::td_api::make_object<td::td_api::setLogVerbosityLevel>(1));
    
    clientManager_ = std::make_unique<td::ClientManager>();
    clientId_ = clientManager_->create_client_id();

    auto params = td::td_api::make_object<td::td_api::setTdlibParameters>();
    params->use_test_dc_ = false;
    params->database_directory_ = config.database_directory;
    params->files_directory_ = config.database_directory + "/files";
    params->use_message_database_ = config.use_message_database;
    params->use_secret_chats_ = config.use_secret_chats;
    params->api_id_ = config.api_id;
    params->api_hash_ = config.api_hash;
    params->system_language_code_ = "en";
    params->device_model_ = "Desktop";
    params->system_version_ = "Linux";
    params->application_version_ = "1.0.0";

    auto setParamsFuture = sendQueryTyped<td::td_api::ok>(std::move(params));
    try {
        setParamsFuture.get();
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to set TDLib parameters: " + std::string(e.what()));
    }
}

void TelegramClient::shutdownTdLib() {
    if (clientManager_) {
        clientManager_.reset();  // нет метода close() в некоторых версиях
    }
}

// ------------------------------------------------------------------
// Поток receive
// ------------------------------------------------------------------
void TelegramClient::runReceiveLoop() {
    while (running_.load()) {
        auto response = clientManager_->receive(1.0);
        if (response.object) {
            processResponse(std::move(response));
        }
    }
}

// ------------------------------------------------------------------
// Отправка запросов (исправлено: передаём clientId_)
// ------------------------------------------------------------------
AFuture<std::unique_ptr<td::td_api::Object>> TelegramClient::sendQuery(
    td::td_api::object_ptr<td::td_api::Function> function) {
    if (!clientManager_) {
        throw std::runtime_error("ClientManager is not initialized");
    }
    RequestId requestId = nextRequestId_++;
    auto promise = std::make_shared<std::promise<std::unique_ptr<td::td_api::Object>>>();
    auto future = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingRequests_[requestId] = promise;
    }

    // Передаём clientId_, requestId и function
    clientManager_->send(clientId_, requestId, std::move(function));
    return future;
}

// ------------------------------------------------------------------
// Обработка ответов и обновлений
// ------------------------------------------------------------------
void TelegramClient::processResponse(td::ClientManager::Response response) {
    if (response.request_id == 0) {
        processUpdate(std::move(response.object));
        return;
    }

    std::unique_lock<std::mutex> lock(pendingMutex_);
    auto it = pendingRequests_.find(response.request_id);
    if (it != pendingRequests_.end()) {
        auto promise = it->second;
        pendingRequests_.erase(it);
        lock.unlock();
        // Используем std::move для преобразования td::tl::unique_ptr в std::unique_ptr
        promise->set_value(std::unique_ptr<td::td_api::Object>(response.object.release()));
    }
}

void TelegramClient::processUpdate(td::td_api::object_ptr<td::td_api::Object> update) {
    int32_t id = update->get_id();

    if (id == td::td_api::updateAuthorizationState::ID) {
        auto* updateState = static_cast<td::td_api::updateAuthorizationState*>(update.get());
        auto& state = updateState->authorization_state_;
        int32_t stateId = state->get_id();

        if (stateId == td::td_api::authorizationStateWaitCode::ID) {
            setAuthState(AuthState::WaitingForCode);
        } else if (stateId == td::td_api::authorizationStateWaitPassword::ID) {
            setAuthState(AuthState::WaitingForPassword);
        } else if (stateId == td::td_api::authorizationStateReady::ID) {
            setAuthState(AuthState::LoggedIn);
        } else if (stateId == td::td_api::authorizationStateLoggingOut::ID ||
                   stateId == td::td_api::authorizationStateClosed::ID) {
            setAuthState(AuthState::LoggedOut);
        }
    }
    else if (id == td::td_api::updateNewMessage::ID) {
        auto* updateMsg = static_cast<td::td_api::updateNewMessage*>(update.get());
        auto& message = updateMsg->message_;
        if (message->content_->get_id() == td::td_api::messageText::ID) {
            auto* textContent = static_cast<td::td_api::messageText*>(message->content_.get());
            int64_t chatId = message->chat_id_;
            int64_t messageId = message->id_;
            std::string text = textContent->text_->text_;

            std::lock_guard<std::mutex> lock(listenerMutex_);
            for (auto& entry : messageListeners_) {
                entry.callback(chatId, messageId, text);
            }
        }
    }
    else if (id == td::td_api::updateMessageSendSucceeded::ID) {
        auto* updateSend = static_cast<td::td_api::updateMessageSendSucceeded*>(update.get());
        std::lock_guard<std::mutex> lock(deliveryMutex_);
        if (deliveryCallback_) {
            // updateSend->message_ — указатель на сообщение
            deliveryCallback_(updateSend->message_->id_, MessageStatus::Delivered);
        }
    }
    else if (id == td::td_api::updateMessageSendFailed::ID) {
        auto* updateFail = static_cast<td::td_api::updateMessageSendFailed*>(update.get());
        std::lock_guard<std::mutex> lock(deliveryMutex_);
        if (deliveryCallback_) {
            // Используем поле message_id (без подчёркивания) – оно есть в этой версии
            // Если его нет, можно взять updateFail->message_->id_
            // В новых версиях TDLib поле называется message_id (int64)
            deliveryCallback_(updateFail->message_->id_, MessageStatus::Failed);
        }
    }
}

// ------------------------------------------------------------------
// Авторизация
// ------------------------------------------------------------------
AFuture<void> TelegramClient::login(const std::string& phoneNumber) {
    if (authState_ != AuthState::LoggedOut) {
        throw std::logic_error("Already logging in or logged in");
    }
    auto request = td::td_api::make_object<td::td_api::setAuthenticationPhoneNumber>();
    request->phone_number_ = phoneNumber;
    auto future = sendQueryTyped<td::td_api::ok>(std::move(request));
    return std::async(std::launch::async, [future = std::move(future)]() mutable {
        future.get();
    });
}

AFuture<void> TelegramClient::setAuthCode(const std::string& code) {
    if (authState_ != AuthState::WaitingForCode) {
        throw std::logic_error("Not waiting for code");
    }
    auto request = td::td_api::make_object<td::td_api::checkAuthenticationCode>();
    request->code_ = code;
    auto future = sendQueryTyped<td::td_api::ok>(std::move(request));
    return std::async(std::launch::async, [this, future = std::move(future)]() mutable {
        try {
            future.get();
        } catch (...) {
            setAuthState(AuthState::Error);
            throw;
        }
    });
}

AFuture<void> TelegramClient::setPassword(const std::string& password) {
    if (authState_ != AuthState::WaitingForPassword) {
        throw std::logic_error("Not waiting for password");
    }
    auto request = td::td_api::make_object<td::td_api::checkAuthenticationPassword>();
    request->password_ = password;
    auto future = sendQueryTyped<td::td_api::ok>(std::move(request));
    return std::async(std::launch::async, [this, future = std::move(future)]() mutable {
        try {
            future.get();
        } catch (...) {
            setAuthState(AuthState::Error);
            throw;
        }
    });
}

// ------------------------------------------------------------------
// Отправка сообщений (исправлено: entities_ не присваиваем)
// ------------------------------------------------------------------
AFuture<MessageResult> TelegramClient::sendMessageImpl(int64_t chatId, const std::string& text) {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }

    auto request = td::td_api::make_object<td::td_api::sendMessage>();
    request->chat_id_ = chatId;
    request->reply_to_ = nullptr;
    request->options_ = td::td_api::make_object<td::td_api::messageSendOptions>();
    request->options_->disable_notification_ = false;
    request->options_->from_background_ = false;
    request->options_->scheduling_state_ = nullptr;

    // Создаём formattedText без явного присваивания entities_ (он пуст по умолчанию)
    auto formattedText = td::td_api::make_object<td::td_api::formattedText>();
    formattedText->text_ = text;
    // entities_ уже пуст (std::vector по умолчанию)

    auto content = td::td_api::make_object<td::td_api::inputMessageText>();
    content->text_ = std::move(formattedText);
    content->link_preview_options_ = nullptr;

    request->input_message_content_ = std::move(content);

    auto future = sendQueryTyped<td::td_api::message>(std::move(request));
    return std::async(std::launch::async, [future = std::move(future)]() mutable -> MessageResult {
        auto msg = future.get();  // msg — объект td::td_api::message
        MessageResult result;
        result.messageId = msg.id_;
        result.finalStatus = MessageStatus::Sent;
        return result;
    });
}

AFuture<MessageResult> TelegramClient::sendMessage(int64_t chatId, const std::string& text) {
    auto operation = [this, chatId, text]() -> AFuture<MessageResult> {
        return sendMessageImpl(chatId, text);
    };
    return executeWithRetry<MessageResult>(operation);
}

// ------------------------------------------------------------------
// Редактирование и удаление
// ------------------------------------------------------------------
AFuture<void> TelegramClient::editMessage(int64_t chatId, int64_t messageId, const std::string& newText) {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }

    auto request = td::td_api::make_object<td::td_api::editMessageText>();
    request->chat_id_ = chatId;
    request->message_id_ = messageId;
    request->reply_markup_ = nullptr;

    auto formattedText = td::td_api::make_object<td::td_api::formattedText>();
    formattedText->text_ = newText;
    // entities_ пуст

    auto content = td::td_api::make_object<td::td_api::inputMessageText>();
    content->text_ = std::move(formattedText);
    content->link_preview_options_ = nullptr;
    request->input_message_content_ = std::move(content);

    auto future = sendQueryTyped<td::td_api::message>(std::move(request));
    return std::async(std::launch::async, [future = std::move(future)]() mutable {
        future.get(); // ждём успешного завершения
    });
}

AFuture<void> TelegramClient::deleteMessage(int64_t chatId, int64_t messageId) {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }

    auto request = td::td_api::make_object<td::td_api::deleteMessages>();
    request->chat_id_ = chatId;
    request->message_ids_ = {messageId};
    request->revoke_ = true;

    auto future = sendQueryTyped<td::td_api::ok>(std::move(request));
    return std::async(std::launch::async, [future = std::move(future)]() mutable {
        future.get();
    });
}

// ------------------------------------------------------------------
// Получение списка чатов (заглушка)
// ------------------------------------------------------------------
AFuture<std::vector<ChatInfo>> TelegramClient::getChats() {
    if (authState_ != AuthState::LoggedIn) {
        throw std::logic_error("Not logged in");
    }
    std::promise<std::vector<ChatInfo>> p;
    p.set_value({});
    return p.get_future();
}

// ------------------------------------------------------------------
// Подписки на события
// ------------------------------------------------------------------
std::uint64_t TelegramClient::addMessageListener(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(listenerMutex_);
    std::uint64_t id = nextListenerId_++;
    messageListeners_.push_back({id, std::move(callback)});
    return id;
}

void TelegramClient::removeMessageListener(std::uint64_t listenerId) {
    std::lock_guard<std::mutex> lock(listenerMutex_);
    auto it = std::find_if(messageListeners_.begin(), messageListeners_.end(),
                           [listenerId](const ListenerEntry& e) { return e.id == listenerId; });
    if (it != messageListeners_.end()) {
        messageListeners_.erase(it);
    }
}

void TelegramClient::onMessageDelivered(DeliveryCallback callback) {
    std::lock_guard<std::mutex> lock(deliveryMutex_);
    deliveryCallback_ = std::move(callback);
}

void TelegramClient::onAuthStateChanged(AuthStateCallback callback) {
    std::lock_guard<std::mutex> lock(authCallbackMutex_);
    authStateCallback_ = std::move(callback);
}

// ------------------------------------------------------------------
// Состояние
// ------------------------------------------------------------------
AuthState TelegramClient::getAuthState() const {
    return authState_.load();
}

void TelegramClient::setAuthState(AuthState newState) {
    authState_ = newState;
    std::lock_guard<std::mutex> lock(authCallbackMutex_);
    if (authStateCallback_) {
        authStateCallback_(newState);
    }
}

void TelegramClient::setRetryPolicy(const RetryPolicy& policy) {
    retryPolicy_ = policy;
}

// ------------------------------------------------------------------
// Шаблон executeWithRetry (определён здесь, объявлен в .h)
// ------------------------------------------------------------------
template<typename T>
AFuture<T> TelegramClient::executeWithRetry(std::function<AFuture<T>()> operation) {
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
                    throw;
                }
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