// tests/TelegramClientTests.cpp
#include <gtest/gtest.h>
#include "MockTelegramClient.h"
#include <future>
#include <chrono>

using namespace telegram;

class TelegramClientTest : public ::testing::Test {
protected:
    MockTelegramClient client;
};

TEST_F(TelegramClientTest, LoginSuccess) {
    client.setExpectedCode("12345");
    EXPECT_EQ(client.getAuthState(), AuthState::LoggedOut);

    auto loginFut = client.login("+123");
    loginFut.wait();
    EXPECT_EQ(client.getAuthState(), AuthState::WaitingForCode);

    auto codeFut = client.setAuthCode("12345");
    codeFut.wait();
    EXPECT_EQ(client.getAuthState(), AuthState::LoggedIn);
}

TEST_F(TelegramClientTest, LoginInvalidCode) {
    client.setExpectedCode("12345");
    client.login("+1").wait();
    EXPECT_THROW(client.setAuthCode("wrong").wait(), std::runtime_error);
    EXPECT_EQ(client.getAuthState(), AuthState::Error);
}

TEST_F(TelegramClientTest, SendMessageAfterLogin) {
    client.setExpectedCode("12345");
    client.login("+1").wait();
    client.setAuthCode("12345").wait();

    auto result = client.sendMessage(42, "Hello").get();
    EXPECT_GT(result.messageId, 0);
    EXPECT_EQ(result.finalStatus, MessageStatus::Sent);
}

TEST_F(TelegramClientTest, SendMessageWithInvalidChatId) {
    client.setExpectedCode("12345");
    client.login("+1").wait();
    client.setAuthCode("12345").wait();

    EXPECT_THROW(client.sendMessage(0, "test").wait(), std::invalid_argument);
}

TEST_F(TelegramClientTest, EditAndDeleteMessage) {
    client.setExpectedCode("12345");
    client.login("+1").wait();
    client.setAuthCode("12345").wait();

    auto msgId = client.sendMessage(100, "old").get().messageId;
    EXPECT_NO_THROW(client.editMessage(100, msgId, "new").wait());
    EXPECT_NO_THROW(client.deleteMessage(100, msgId).wait());
}

TEST_F(TelegramClientTest, GetChats) {
    client.setExpectedCode("12345");
    client.login("+1").wait();
    client.setAuthCode("12345").wait();

    auto chats = client.getChats().get();
    ASSERT_GE(chats.size(), 2);
    EXPECT_EQ(chats[0].id, 100);
}

TEST_F(TelegramClientTest, MultipleMessageListeners) {
    int callCount = 0;
    auto id1 = client.addMessageListener([&](int64_t, int64_t, const std::string&) { callCount++; });
    auto id2 = client.addMessageListener([&](int64_t, int64_t, const std::string&) { callCount++; });

    client.simulateIncomingMessage(1, 2, "test");
    EXPECT_EQ(callCount, 2);

    client.removeMessageListener(id1);
    client.simulateIncomingMessage(3, 4, "another");
    EXPECT_EQ(callCount, 3);
}

TEST_F(TelegramClientTest, DeliveryCallback) {
    bool delivered = false;
    client.onMessageDelivered([&](int64_t id, MessageStatus status) {
        delivered = true;
        EXPECT_EQ(status, MessageStatus::Sent);
    });

    client.setExpectedCode("12345");
    client.login("+1").wait();
    client.setAuthCode("12345").wait();

    auto result = client.sendMessage(10, "test").get();
    EXPECT_TRUE(delivered);
}

TEST_F(TelegramClientTest, AuthStateCallback) {
    AuthState reported = AuthState::LoggedOut;
    client.onAuthStateChanged([&](AuthState state) { reported = state; });

    client.setAuthState(AuthState::LoggedIn);
    EXPECT_EQ(reported, AuthState::LoggedIn);
}

TEST_F(TelegramClientTest, Logout) {
    client.setExpectedCode("12345");
    client.login("+1").wait();
    client.setAuthCode("12345").wait();
    EXPECT_EQ(client.getAuthState(), AuthState::LoggedIn);

    client.logout().wait();
    EXPECT_EQ(client.getAuthState(), AuthState::LoggedOut);
}

TEST_F(TelegramClientTest, ConnectDisconnect) {
    TdConfig cfg;
    cfg.api_id = 123;
    cfg.api_hash = "hash";
    client.connect(cfg).wait();
    EXPECT_EQ(client.getAuthState(), AuthState::LoggedOut);

    client.disconnect().wait();
    EXPECT_EQ(client.getAuthState(), AuthState::LoggedOut);
}

// Исправленный тест с колбэком (ранее был onNewMessage)
TEST_F(TelegramClientTest, NewMessageCallback) {
    bool callbackCalled = false;
    int64_t expectedChat = 999;
    int64_t expectedMsg = 888;
    std::string expectedText = "incoming";

    auto listenerId = client.addMessageListener(
        [&](int64_t chatId, int64_t messageId, const std::string& text) {
            callbackCalled = true;
            EXPECT_EQ(chatId, expectedChat);
            EXPECT_EQ(messageId, expectedMsg);
            EXPECT_EQ(text, expectedText);
        }
    );

    client.simulateIncomingMessage(expectedChat, expectedMsg, expectedText);
    EXPECT_TRUE(callbackCalled);
    client.removeMessageListener(listenerId);
}