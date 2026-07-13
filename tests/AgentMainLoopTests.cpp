// tests/AgentMainLoopTests.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <future>

#include "core/AgentMainLoop.h"
#include "telegram/MockTelegramClient.h"
#include "event/MockEventQueue.h"
#include "common/ILogger.h"
#include "event/PriorityResolver.h"

using namespace core;
using namespace event;
using namespace telegram;
using namespace testing;   // для удобства: _, Return, Throw

// --- Мок-обработчик для тестов ---
class MockMessageHandler : public IMessageHandler {
public:
    MOCK_METHOD(std::string, handle, (const event::Event&), (override));
};

// --- Тестовый логгер ---
class TestLogger : public common::ILogger {
public:
    std::vector<std::string> infos, warns, errors, debugs;
    void info(const std::string& msg) override { infos.push_back(msg); }
    void warn(const std::string& msg) override { warns.push_back(msg); }
    void error(const std::string& msg) override { errors.push_back(msg); }
    void debug(const std::string& msg) override { debugs.push_back(msg); }
};

// ------------------------------------------------------------
// Тест: запуск и остановка
// ------------------------------------------------------------
TEST(AgentMainLoopTest, StartStop) {
    auto client = std::make_shared<MockTelegramClient>();
    auto queue = std::make_shared<MockEventQueue>();
    auto handler = std::make_shared<MockMessageHandler>();
    auto resolver = std::make_shared<PriorityResolver>();   // добавлено
    auto logger = std::make_shared<TestLogger>();

    AgentMainLoop loop(client, queue, handler, resolver, 0, "all", logger);   // 5 аргументов

    std::thread loopThread([&loop]() { loop.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(loop.isRunning());

    loop.stop();
    loopThread.join();
    EXPECT_FALSE(loop.isRunning());
}

// ------------------------------------------------------------
// Тест: обработка входящего сообщения (единственный, исправленный)
// ------------------------------------------------------------
TEST(AgentMainLoopTest, ProcessIncomingMessage) {
    auto client = std::make_shared<MockTelegramClient>();
    auto queue = std::make_shared<MockEventQueue>();
    auto handler = std::make_shared<MockMessageHandler>();
    auto resolver = std::make_shared<PriorityResolver>();
    auto logger = std::make_shared<TestLogger>();

    client->setAuthState(AuthState::LoggedIn);   // <-- добавлено

    EXPECT_CALL(*handler, handle(_))
        .WillOnce(Return("Hello back"));

    AgentMainLoop loop(client, queue, handler, resolver, 0, "all", logger);
    std::thread loopThread([&loop]() { loop.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client->simulateIncomingMessage(123, 456, "Hello");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Проверяем, что клиент отправил сообщение (требует getSentMessages в моке)
    const auto& sent = client->getSentMessages();
    ASSERT_EQ(sent.size(), 1);
    EXPECT_EQ(sent[0].first, 123);
    EXPECT_EQ(sent[0].second, "Hello back");

    loop.stop();
    loopThread.join();
}

// ------------------------------------------------------------
// Тест: graceful shutdown при вызове stop
// ------------------------------------------------------------
TEST(AgentMainLoopTest, ShutdownOnStop) {
    auto client = std::make_shared<MockTelegramClient>();
    auto queue = std::make_shared<MockEventQueue>();
    auto handler = std::make_shared<MockMessageHandler>();
    auto resolver = std::make_shared<PriorityResolver>();
    auto logger = std::make_shared<TestLogger>();

    AgentMainLoop loop(client, queue, handler, resolver, 0, "all", logger);
    std::thread loopThread([&loop]() { loop.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();
    loopThread.join();
    EXPECT_FALSE(loop.isRunning());
}

// ------------------------------------------------------------
// Тест: ошибка в обработчике не прерывает цикл
// ------------------------------------------------------------
TEST(AgentMainLoopTest, HandlerErrorDoesNotCrash) {
    auto client = std::make_shared<MockTelegramClient>();
    auto queue = std::make_shared<MockEventQueue>();
    auto handler = std::make_shared<MockMessageHandler>();
    auto resolver = std::make_shared<PriorityResolver>();
    auto logger = std::make_shared<TestLogger>();

    EXPECT_CALL(*handler, handle(_))
        .WillOnce(Throw(std::runtime_error("Handler crash")));

    AgentMainLoop loop(client, queue, handler, resolver, 0, "all", logger);
    std::thread loopThread([&loop]() { loop.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    TelegramMessage msg{999, 888, "test"};
    Event ev(EventPriority::Normal, "telegram", msg);
    queue->push(ev);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(loop.isRunning());

    loop.stop();
    loopThread.join();
}