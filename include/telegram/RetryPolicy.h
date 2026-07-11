// include/telegram/RetryPolicy.h
#pragma once
#include <chrono>

namespace telegram {

struct RetryPolicy {
    int max_attempts = 3;
    std::chrono::milliseconds initial_delay{100};
    double backoff_multiplier = 2.0;
    bool exponential = true;
};

} // namespace telegram