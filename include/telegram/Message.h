// include/telegram/Message.h
#pragma once
#include <string>
#include <chrono>
#include <cstdint>

namespace telegram {

enum class MessageStatus {
    Sending,
    Sent,
    Delivered,
    Read,
    Failed
};

struct Message {
    int64_t id;
    int64_t chatId;
    std::string text;
    std::chrono::system_clock::time_point timestamp;
    MessageStatus status = MessageStatus::Sending;
};

struct MessageResult {
    int64_t messageId;
    MessageStatus finalStatus; // для простоты, в реальности может быть future<Status>
};

} // namespace telegram