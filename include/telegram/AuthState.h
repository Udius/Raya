// include/telegram/AuthState.h
#pragma once

namespace telegram {

enum class AuthState {
    LoggedOut,
    WaitingForCode,
    WaitingForPassword,
    LoggedIn,
    Error
};

} // namespace telegram