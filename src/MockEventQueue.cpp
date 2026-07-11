// src/MockEventQueue.cpp
#include "event/MockEventQueue.h"
#include <stdexcept>
#include <thread>
#include <chrono>

namespace event {

MockEventQueue::MockEventQueue(std::shared_ptr<common::ILogger> logger)
    : logger_(std::move(logger)) {
    if (!logger_) logger_ = std::make_shared<common::NullLogger>();
}

void MockEventQueue::push(Event event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isShutdown_) {
        logger_->warn("MockEventQueue push after shutdown, ignored");
        return;
    }
    events_.push_back(std::move(event));
    logger_->debug("Mock push, size=" + std::to_string(events_.size()));
}

Event MockEventQueue::pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isShutdown_) {
        logger_->debug("Mock pop after shutdown, throw");
        throw ShutdownException();
    }
    if (events_.empty()) {
        logger_->error("Mock pop on empty queue");
        throw std::runtime_error("MockEventQueue is empty");
    }
    Event event = std::move(events_.back());
    events_.pop_back();
    logger_->debug("Mock pop, remaining size=" + std::to_string(events_.size()));
    return event;
}

std::optional<Event> MockEventQueue::tryPop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) {
        return std::nullopt;
    }
    Event event = std::move(events_.back());
    events_.pop_back();
    logger_->debug("Mock tryPop succeeded");
    return event;
}

std::optional<Event> MockEventQueue::tryPopFor(std::chrono::milliseconds timeout) {
    // Имитация ожидания – просто спим, затем пробуем tryPop
    // В реальном моке можно использовать условие, но для простоты – sleep
    std::this_thread::sleep_for(timeout);
    return tryPop(); // используем уже существующий tryPop
}

size_t MockEventQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
}

bool MockEventQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.empty();
}

void MockEventQueue::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    isShutdown_ = true;
    logger_->info("Mock queue shutdown");
}

void MockEventQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
    logger_->debug("Mock queue cleared");
}

const std::vector<Event>& MockEventQueue::getEvents() const {
    return events_;
}

} // namespace event