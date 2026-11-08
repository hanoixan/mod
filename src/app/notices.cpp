#include "app/notices.hpp"

#include <utility>

namespace mod {

void Notices::post(const void* key, std::string text, std::optional<std::chrono::milliseconds> lasts) {
    notices_[key] = Notice{std::move(text), lasts, std::nullopt, Clock::now()};
}

void Notices::clear(const void* key) { notices_.erase(key); }

std::optional<std::string> Notices::show(const void* key, Clock::time_point now) {
    const auto it = notices_.find(key);
    if (it == notices_.end()) return std::nullopt;
    Notice& n = it->second;
    if (n.lasts && !n.deadline) n.deadline = now + *n.lasts;
    if (n.deadline && now >= *n.deadline) {
        notices_.erase(it);
        return std::nullopt;
    }
    return n.text;
}

std::optional<Notices::Clock::time_point> Notices::next_deadline() const {
    std::optional<Clock::time_point> next;
    for (const auto& [key, n] : notices_) {
        if (n.deadline && (!next || *n.deadline < *next)) next = n.deadline;
    }
    return next;
}

std::optional<Notices::Clock::time_point> Notices::posted(const void* key) const {
    const auto it = notices_.find(key);
    if (it == notices_.end()) return std::nullopt;
    return it->second.posted;
}

}  // namespace mod
