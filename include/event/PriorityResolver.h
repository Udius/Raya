// include/event/PriorityResolver.h
#pragma once

#include "EventPriority.h"
#include <cstdint>
#include <unordered_set>

namespace event {

class PriorityResolver {
public:
    PriorityResolver() = default;

    void setPapikChatId(int64_t chatId);
    void addHighPriorityChat(int64_t chatId);
    void removeHighPriorityChat(int64_t chatId);

    EventPriority resolve(int64_t chatId) const;

private:
    int64_t papikChatId_ = 0;
    std::unordered_set<int64_t> highPriorityChats_;
};

} // namespace event