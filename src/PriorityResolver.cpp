// src/PriorityResolver.cpp
#include "event/PriorityResolver.h"

namespace event {

void PriorityResolver::setPapikChatId(int64_t chatId) {
    papikChatId_ = chatId;
}

void PriorityResolver::addHighPriorityChat(int64_t chatId) {
    highPriorityChats_.insert(chatId);
}

void PriorityResolver::removeHighPriorityChat(int64_t chatId) {
    highPriorityChats_.erase(chatId);
}

EventPriority PriorityResolver::resolve(int64_t chatId) const {
    if (chatId == papikChatId_) {
        return EventPriority::Papik;
    }
    if (highPriorityChats_.count(chatId) > 0) {
        return EventPriority::High;
    }
    return EventPriority::Normal; // по умолчанию
}

} // namespace event