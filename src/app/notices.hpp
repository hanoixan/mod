#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>

namespace mod {

// Notices: messages that belong to one thing (an open document) rather than to the moment,
// shown on the status line of the view that has the focus while it shows that thing. A notice
// is posted with an optional lifetime; the lifetime starts when the notice is first shown, so a
// notice for a document out of sight waits for it. One notice per key: posting again replaces
// it. Keys are opaque pointers the owner keeps alive or clears. Main thread.
class Notices {
public:
    using Clock = std::chrono::steady_clock;

    // Replaces the notice for `key`. With `lasts`, it goes that long after it is first shown;
    // without, it stays until cleared.
    void post(const void* key, std::string text, std::optional<std::chrono::milliseconds> lasts = std::nullopt);
    void clear(const void* key);
    // The notice for `key` as it should show at `now`: its timer starts on the first call, and
    // a notice whose time is up is gone (nullopt). Not const: it starts and ends timers.
    std::optional<std::string> show(const void* key, Clock::time_point now);
    bool has(const void* key) const { return notices_.contains(key); }
    // When the next shown notice ends, for the event loop's timeout.
    std::optional<Clock::time_point> next_deadline() const;
    // When the notice for `key` was posted (or nullopt): a status message set after it wins.
    std::optional<Clock::time_point> posted(const void* key) const;
    // Drops every notice whose key `keep` rejects (documents that closed).
    template <class Keep>
    void retain(Keep keep) {
        std::erase_if(notices_, [&](const auto& kv) { return !keep(kv.first); });
    }

private:
    struct Notice {
        std::string text;
        std::optional<std::chrono::milliseconds> lasts;
        std::optional<Clock::time_point> deadline;  // set when first shown, with `lasts`
        Clock::time_point posted;
    };
    std::map<const void*, Notice> notices_;
};

}  // namespace mod
