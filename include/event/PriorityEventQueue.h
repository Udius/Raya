// include/event/PriorityEventQueue.h
#pragma once

#include "IEventQueue.h"
#include "common/ILogger.h"
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

namespace event {

class PriorityEventQueue : public IEventQueue {
public:
    explicit PriorityEventQueue(std::shared_ptr<common::ILogger> logger = std::make_shared<common::NullLogger>());

    void push(Event event) override;
    Event pop() override;
    std::optional<Event> tryPop() override;
    std::optional<Event> tryPopFor(std::chrono::milliseconds timeout) override;
    size_t size() const override;
    bool empty() const override;
    void shutdown() override;

private:
    struct Comparator {
        bool operator()(const Event& a, const Event& b) const;
    };

    std::priority_queue<Event, std::vector<Event>, Comparator> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> isShutdown_{false};
    std::shared_ptr<common::ILogger> logger_;
};

} // namespace event