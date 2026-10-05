#include "util/log.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace mod {
namespace {

struct Sink {
    std::mutex mutex;
    std::FILE* file = nullptr;
    LogLevel threshold = LogLevel::info;
};

Sink& sink() {
    static Sink s;
    return s;
}

std::string_view level_name(LogLevel level) {
    switch (level) {
        case LogLevel::debug: return "debug";
        case LogLevel::info: return "info";
        case LogLevel::warn: return "warn";
        case LogLevel::error: return "error";
    }
    return "?";
}

}  // namespace

void init_logging() {
    Sink& s = sink();
    std::lock_guard lock(s.mutex);
    if (s.file != nullptr) return;
    const char* path = std::getenv("MOD_LOG");
    if (path == nullptr || *path == '\0') return;
    if (const char* lvl = std::getenv("MOD_LOG_LEVEL")) {
        const std::string_view v(lvl);
        if (v == "debug") s.threshold = LogLevel::debug;
        else if (v == "warn") s.threshold = LogLevel::warn;
        else if (v == "error") s.threshold = LogLevel::error;
        else s.threshold = LogLevel::info;
    }
    s.file = std::fopen(path, "ae");  // 'e': O_CLOEXEC, so child processes don't inherit it
}

namespace detail {

bool log_enabled(LogLevel level) noexcept {
    Sink& s = sink();
    std::lock_guard lock(s.mutex);
    return s.file != nullptr && level >= s.threshold;
}

void log_write(LogLevel level, std::string_view message) noexcept {
    try {
        const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
        std::ostringstream tid;
        tid << std::this_thread::get_id();
        const std::string line = std::format("{:%FT%T}Z {} {} {}\n", now, tid.str(), level_name(level), message);
        Sink& s = sink();
        std::lock_guard lock(s.mutex);
        if (s.file == nullptr) return;
        if (std::fwrite(line.data(), 1, line.size(), s.file) != line.size() || std::fflush(s.file) != 0) {
            (void)std::fclose(s.file);  // a write error disables logging for the rest of the process
            s.file = nullptr;
        }
    } catch (...) {  // NOLINT(bugprone-empty-catch): the logger has nowhere to report its own failure
    }
}

}  // namespace detail
}  // namespace mod
