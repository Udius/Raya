// include/telegram/ChatInfo.h
#pragma once
#include <string>
#include <cstdint>

namespace telegram {

struct ChatInfo {
    int64_t id;
    std::string title;
    bool is_group;
};

} // namespace telegram