// include/event/IEventQueue.h
#pragma once

#include "Event.h"
#include <optional>
#include <chrono>
#include <cstddef>
#include <stdexcept>

namespace event {

class IEventQueue {
public:
    virtual ~IEventQueue() = default;

    virtual void push(Event event) = 0;
    virtual Event pop() = 0;
    virtual std::optional<Event> tryPop() = 0;
    virtual std::optional<Event> tryPopFor(std::chrono::milliseconds timeout) = 0; // Новый метод
    virtual size_t size() const = 0;
    virtual bool empty() const = 0;
    virtual void shutdown() = 0;
};

class ShutdownException : public std::runtime_error {
public:
    ShutdownException() : std::runtime_error("EventQueue has been shut down") {}
};

} // namespace event