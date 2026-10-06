// campfire.log — minimal thread-safe stderr logger.
//
// The request hot path does not log; this exists for lifecycle events
// (startup, listen address, fatal errors) only.
export module campfire.log;

import std;

export namespace campfire {

enum class LogLevel : std::uint8_t { debug = 0, info = 1, warn = 2, error = 3 };

void write_log(LogLevel level, std::string_view message);

inline void log_debug(std::string_view message) { write_log(LogLevel::debug, message); }
inline void log_info(std::string_view message)  { write_log(LogLevel::info,  message); }
inline void log_warn(std::string_view message)  { write_log(LogLevel::warn,  message); }
inline void log_error(std::string_view message) { write_log(LogLevel::error, message); }

}
