// tests/EventQueueTests.cpp
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <future>

#include "event/PriorityEventQueue.h"
#include "event/MockEventQueue.h"
#include "event/PriorityResolver.h"
#include "common/ILogger.h"

using namespace event;

// ---- MockLogger для проверки логирования ----
class MockLogger : public common::ILogger {
public:
    std::vector<std::string> infoMsgs, warnMsgs, errorMsgs, debugMsgs;
    void info(const std::string& msg) override { infoMsgs.push_back(msg); }
    void warn(const std::string& msg) override { warnMsgs.push_back(msg); }
    void error(const std::string& msg) override { errorMsgs.push_back(msg); }
    void debug(const std::string& msg) override { debugMsgs.push_back(msg); }
};

// ------------------------------------------------------------
// Тесты для tryPopFor (PriorityEventQueue)
// ------------------------------------------------------------

TEST(PriorityEventQueueTest, TryPopForTimeout) {
    auto logger = std::make_shared<MockLogger>();
    PriorityEventQueue queue(logger);
    auto start = std::chrono::steady_clock::now();
    auto opt = queue.tryPopFor(std::chrono::milliseconds(50));
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_FALSE(opt.has_value());
    EXPECT_GE(elapsed, std::chrono::milliseconds(50));
    EXPECT_FALSE(logger->debugMsgs.empty());
}

TEST(PriorityEventQueueTest, TryPopForReturnsEvent) {
    PriorityEventQueue queue;
    TelegramMessage msg{1, 2, "hello"};
    queue.push(Event{EventPriority::Normal, "telegram", msg});
    auto opt = queue.tryPopFor(std::chrono::milliseconds(100));
    ASSERT_TRUE(opt.has_value());
    auto* payload = std::get_if<TelegramMessage>(&opt->payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->text, "hello");
}

TEST(PriorityEventQueueTest, TryPopForShutdownReturnsNullopt) {
    PriorityEventQueue queue;
    queue.shutdown();
    auto opt = queue.tryPopFor(std::chrono::milliseconds(10));
    EXPECT_FALSE(opt.has_value());
}

// ------------------------------------------------------------
// Тесты для tryPopFor (MockEventQueue)
// ------------------------------------------------------------

TEST(MockEventQueueTest, TryPopForTimeout) {
    MockEventQueue queue;
    auto start = std::chrono::steady_clock::now();
    auto opt = queue.tryPopFor(std::chrono::milliseconds(30));
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_FALSE(opt.has_value());
    EXPECT_GE(elapsed, std::chrono::milliseconds(30));
}

TEST(MockEventQueueTest, TryPopForReturnsEvent) {
    MockEventQueue queue;
    TelegramMessage msg{1, 2, "world"};
    queue.push(Event{EventPriority::Normal, "telegram", msg});
    auto opt = queue.tryPopFor(std::chrono::milliseconds(10));
    ASSERT_TRUE(opt.has_value());
    auto* payload = std::get_if<TelegramMessage>(&opt->payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->text, "world");
}

// ------------------------------------------------------------
// Тесты для Event с variant
// ------------------------------------------------------------

TEST(EventTest, VariantPayload) {
    TelegramMessage msg{100, 200, "test"};
    Event ev(EventPriority::Normal, "telegram", msg);
    EXPECT_EQ(ev.source, "telegram");
    EXPECT_EQ(ev.getText(), "test");

    TimerEvent timer{"tick"};
    Event ev2(EventPriority::Background, "timer", timer);
    EXPECT_TRUE(std::holds_alternative<TimerEvent>(ev2.payload));
    EXPECT_EQ(std::get<TimerEvent>(ev2.payload).tag, "tick");

    InternalEvent internal{"shutdown"};
    Event ev3(EventPriority::Critical, "internal", internal);
    EXPECT_TRUE(std::holds_alternative<InternalEvent>(ev3.payload));
    EXPECT_EQ(std::get<InternalEvent>(ev3.payload).command, "shutdown");
}

// ------------------------------------------------------------
// Тесты для логирования
// ------------------------------------------------------------

TEST(PriorityEventQueueTest, LoggingOnPushAndPop) {
    auto logger = std::make_shared<MockLogger>();
    PriorityEventQueue queue(logger);
    TelegramMessage msg{1, 1, "log test"};
    queue.push(Event{EventPriority::Normal, "telegram", msg});
    EXPECT_FALSE(logger->debugMsgs.empty());

    auto ev = queue.pop();
    EXPECT_GE(logger->debugMsgs.size(), 2);
}

TEST(PriorityEventQueueTest, LoggingOnShutdown) {
    auto logger = std::make_shared<MockLogger>();
    PriorityEventQueue queue(logger);
    queue.shutdown();
    EXPECT_FALSE(logger->infoMsgs.empty());
    EXPECT_TRUE(logger->infoMsgs[0].find("shutdown") != std::string::npos);
}

// ------------------------------------------------------------
// Тесты на приоритетность
// ------------------------------------------------------------

TEST(PriorityEventQueueTest, PriorityOrderWithVariant) {
    PriorityEventQueue queue;
    TelegramMessage msg1{1, 1, "normal"};
    TelegramMessage msg2{2, 2, "high"};
    TelegramMessage msg3{3, 3, "critical"};

    queue.push(Event{EventPriority::Normal, "telegram", msg1});
    queue.push(Event{EventPriority::High, "telegram", msg2});
    queue.push(Event{EventPriority::Critical, "telegram", msg3});

    EXPECT_EQ(queue.pop().priority, EventPriority::Critical);
    EXPECT_EQ(queue.pop().priority, EventPriority::High);
    EXPECT_EQ(queue.pop().priority, EventPriority::Normal);
}

TEST(PriorityEventQueueTest, PriorityOrder) {
    PriorityEventQueue queue;
    TelegramMessage msg1{1, 1, "normal"};
    TelegramMessage msg2{2, 2, "high"};
    TelegramMessage msg3{3, 3, "critical"};

    queue.push(Event{EventPriority::Normal, "telegram", msg1});
    queue.push(Event{EventPriority::High, "telegram", msg2});
    queue.push(Event{EventPriority::Critical, "telegram", msg3});

    EXPECT_EQ(queue.pop().priority, EventPriority::Critical);
    EXPECT_EQ(queue.pop().priority, EventPriority::High);
    EXPECT_EQ(queue.pop().priority, EventPriority::Normal);
    EXPECT_TRUE(queue.empty());
}

TEST(PriorityEventQueueTest, PopBlocksUntilEvent) {
    PriorityEventQueue queue;
    auto future = std::async(std::launch::async, [&queue]() {
        return queue.pop(); // блокируется
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

    TelegramMessage msg{1, 1, "hello"};
    queue.push(Event{EventPriority::Normal, "telegram", msg});

    auto result = future.get();
    EXPECT_EQ(result.getText(), "hello");
}

TEST(PriorityEventQueueTest, ShutdownWakesPop) {
    PriorityEventQueue queue;
    auto future = std::async(std::launch::async, [&queue]() {
        EXPECT_THROW(queue.pop(), ShutdownException);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.shutdown();
    future.get();
}

TEST(PriorityEventQueueTest, TryPopReturnsNulloptWhenEmpty) {
    PriorityEventQueue queue;
    auto opt = queue.tryPop();
    EXPECT_FALSE(opt.has_value());
}

TEST(PriorityEventQueueTest, TryPopReturnsEventWhenAvailable) {
    PriorityEventQueue queue;
    TelegramMessage msg{1, 1, "hello"};
    queue.push(Event{EventPriority::Normal, "telegram", msg});
    auto opt = queue.tryPop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->getText(), "hello");
}

TEST(PriorityEventQueueTest, SizeAndEmpty) {
    PriorityEventQueue queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);

    TelegramMessage msg{1, 1, "msg"};
    queue.push(Event{EventPriority::Normal, "telegram", msg});
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);

    queue.pop();
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
}

TEST(PriorityEventQueueTest, MultithreadedPushPop) {
    PriorityEventQueue queue;
    const int numProducers = 4;
    const int numMessagesPerProducer = 100;
    std::vector<std::future<void>> producers;
    std::vector<std::future<void>> consumers;

    std::atomic<int> receivedCount{0};
    for (int i = 0; i < 4; ++i) {
        consumers.emplace_back(std::async(std::launch::async, [&queue, &receivedCount, numProducers, numMessagesPerProducer]() {
            int total = numProducers * numMessagesPerProducer;
            for (int j = 0; j < total / 4; ++j) {
                auto ev = queue.pop();
                receivedCount++;
            }
        }));
    }

    for (int i = 0; i < numProducers; ++i) {
        producers.emplace_back(std::async(std::launch::async, [&queue, numMessagesPerProducer]() {
            for (int j = 0; j < numMessagesPerProducer; ++j) {
                TelegramMessage msg{0, 0, "msg"};
                queue.push(Event{EventPriority::Normal, "telegram", msg});
            }
        }));
    }

    for (auto& f : producers) f.get();
    for (auto& f : consumers) f.get();

    EXPECT_EQ(receivedCount.load(), numProducers * numMessagesPerProducer);
    EXPECT_TRUE(queue.empty());
}

// ------------------------------------------------------------
// Тесты для MockEventQueue
// ------------------------------------------------------------

TEST(MockEventQueueTest, PushAndPop) {
    MockEventQueue queue;
    TelegramMessage msg{1, 1, "hello"};
    queue.push(Event{EventPriority::Normal, "telegram", msg});
    EXPECT_EQ(queue.size(), 1);
    auto popped = queue.pop();
    EXPECT_EQ(popped.getText(), "hello");
    EXPECT_TRUE(queue.empty());
}

TEST(MockEventQueueTest, TryPop) {
    MockEventQueue queue;
    auto opt = queue.tryPop();
    EXPECT_FALSE(opt.has_value());

    TelegramMessage msg{1, 1, "hello"};
    queue.push(Event{EventPriority::Normal, "telegram", msg});
    opt = queue.tryPop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->getText(), "hello");
}

TEST(MockEventQueueTest, PopThrowsWhenEmpty) {
    MockEventQueue queue;
    EXPECT_THROW(queue.pop(), std::runtime_error);
}

TEST(MockEventQueueTest, ShutdownThrowsOnPop) {
    MockEventQueue queue;
    queue.shutdown();
    EXPECT_THROW(queue.pop(), ShutdownException);
}

TEST(MockEventQueueTest, ClearAndGetEvents) {
    MockEventQueue queue;
    TelegramMessage msg1{1, 1, "one"};
    TelegramMessage msg2{2, 2, "two"};
    queue.push(Event{EventPriority::Normal, "telegram", msg1});
    queue.push(Event{EventPriority::Normal, "telegram", msg2});
    EXPECT_EQ(queue.size(), 2);
    const auto& events = queue.getEvents();
    EXPECT_EQ(events.size(), 2);

    queue.clear();
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.getEvents().size(), 0);
}

// ------------------------------------------------------------
// Тесты для PriorityResolver
// ------------------------------------------------------------

TEST(PriorityResolverTest, Resolve) {
    PriorityResolver resolver;
    resolver.setPapikChatId(100);
    resolver.addHighPriorityChat(200);
    resolver.addHighPriorityChat(201);

    EXPECT_EQ(resolver.resolve(100), EventPriority::Papik);
    EXPECT_EQ(resolver.resolve(200), EventPriority::High);
    EXPECT_EQ(resolver.resolve(201), EventPriority::High);
    EXPECT_EQ(resolver.resolve(300), EventPriority::Normal);
}

TEST(PriorityResolverTest, RemoveHighPriority) {
    PriorityResolver resolver;
    resolver.addHighPriorityChat(200);
    EXPECT_EQ(resolver.resolve(200), EventPriority::High);
    resolver.removeHighPriorityChat(200);
    EXPECT_EQ(resolver.resolve(200), EventPriority::Normal);
}