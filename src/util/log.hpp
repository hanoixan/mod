#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace mod {

enum class LogLevel { debug, info, warn, error };

// Reads MOD_LOG (a path) and MOD_LOG_LEVEL (debug|info|warn|error, default info).
void init_logging();

namespace detail {
bool log_enabled(LogLevel level) noexcept;
void log_write(LogLevel level, std::string_view message) noexcept;
}  // namespace detail

// Appends "time, thread id, level, message" when logging is on and `level` passes
// the threshold. Any thread; never writes to the terminal.
template <class... Args>
void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
    if (!detail::log_enabled(level)) return;
    try {
        detail::log_write(level, std::format(fmt, std::forward<Args>(args)...));
    } catch (...) {  // NOLINT(bugprone-empty-catch): the logger cannot log its own failure
        // Formatting failures are dropped: logging must never throw.
    }
}

}  // namespace mod
