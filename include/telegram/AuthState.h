// include/telegram/AuthState.h
#pragma once

namespace telegram {

enum class AuthState {
    LoggedOut,
    WaitingForPhoneNumber,
    WaitingForCode,
    WaitingForPassword,
    LoggedIn,
    Error
};

} // namespace telegram