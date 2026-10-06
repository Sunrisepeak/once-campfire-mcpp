module;
#include <cstdio>
module campfire.log;

import std;

namespace campfire {

namespace {

std::string_view level_name_(LogLevel level) {
    switch (level) {
        case LogLevel::debug: return "debug";
        case LogLevel::info:  return "info";
        case LogLevel::warn:  return "warn";
        case LogLevel::error: return "error";
    }
    return "?";
}

std::int64_t epoch_millis_() {
    const auto now { std::chrono::system_clock::now().time_since_epoch() };
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::mutex gLogMutex { };

}  // namespace

void write_log(LogLevel level, std::string_view message) {
    const auto stamp { epoch_millis_() };
    const std::lock_guard lock { gLogMutex };
    std::println(stderr, "[{:>14}.{:03}] [{}] {}", stamp / 1000, stamp % 1000,
                 level_name_(level), message);
}

}
