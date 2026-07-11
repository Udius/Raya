// include/telegram/TdConfig.h
#pragma once
#include <string>

namespace telegram {

struct TdConfig {
    int api_id = 0;
    std::string api_hash;
    std::string database_directory = "./tdlib_session";
    bool use_message_database = true;
    bool use_secret_chats = false;
};

} // namespace telegram