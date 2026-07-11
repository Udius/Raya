// include/event/MockEventQueue.h
#pragma once

#include "IEventQueue.h"
#include "common/ILogger.h"
#include <vector>
#include <mutex>
#include <optional>
#include <memory>

namespace event {

class MockEventQueue : public IEventQueue {
public:
    explicit MockEventQueue(std::shared_ptr<common::ILogger> logger = std::make_shared<common::NullLogger>());

    void push(Event event) override;
    Event pop() override;
    std::optional<Event> tryPop() override;
    std::optional<Event> tryPopFor(std::chrono::milliseconds timeout) override;
    size_t size() const override;
    bool empty() const override;
    void shutdown() override;

    // Дополнительные методы для тестов
    void clear();
    const std::vector<Event>& getEvents() const;

private:
    std::vector<Event> events_;
    mutable std::mutex mutex_;
    bool isShutdown_ = false;
    std::shared_ptr<common::ILogger> logger_;
};

} // namespace event