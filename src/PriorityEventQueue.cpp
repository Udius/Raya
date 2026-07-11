// src/PriorityEventQueue.cpp
#include "event/PriorityEventQueue.h"
#include <chrono>

namespace event {

PriorityEventQueue::PriorityEventQueue(std::shared_ptr<common::ILogger> logger)
    : logger_(std::move(logger)) {
    if (!logger_) logger_ = std::make_shared<common::NullLogger>();
}

bool PriorityEventQueue::Comparator::operator()(const Event& a, const Event& b) const {
    return static_cast<int>(a.priority) < static_cast<int>(b.priority);
}

void PriorityEventQueue::push(Event event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (isShutdown_.load()) {
            logger_->warn("push() called after shutdown, event ignored");
            return;
        }
        queue_.push(std::move(event));
        logger_->debug("Event pushed, queue size=" + std::to_string(queue_.size()));
    }
    cv_.notify_one();
}

Event PriorityEventQueue::pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] {
        return !queue_.empty() || isShutdown_.load();
    });
    if (isShutdown_.load() && queue_.empty()) {
        logger_->debug("pop() interrupted by shutdown");
        throw ShutdownException();
    }
    Event event = std::move(const_cast<Event&>(queue_.top()));
    queue_.pop();
    logger_->debug("Event popped, remaining size=" + std::to_string(queue_.size()));
    return event;
}

std::optional<Event> PriorityEventQueue::tryPop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return std::nullopt;
    }
    Event event = std::move(const_cast<Event&>(queue_.top()));
    queue_.pop();
    logger_->debug("tryPop() succeeded, remaining size=" + std::to_string(queue_.size()));
    return event;
}

std::optional<Event> PriorityEventQueue::tryPopFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool ready = cv_.wait_for(lock, timeout, [this] {
        return !queue_.empty() || isShutdown_.load();
    });
    if (!ready || isShutdown_.load()) {
        logger_->debug("tryPopFor() timeout or shutdown, returning nullopt");
        return std::nullopt;
    }
    Event event = std::move(const_cast<Event&>(queue_.top()));
    queue_.pop();
    logger_->debug("tryPopFor() succeeded, remaining size=" + std::to_string(queue_.size()));
    return event;
}

size_t PriorityEventQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool PriorityEventQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

void PriorityEventQueue::shutdown() {
    isShutdown_.store(true);
    cv_.notify_all();
    logger_->info("Queue shutdown");
}

} // namespace event