#include "app/read_only.hpp"

#include <utility>

namespace mod {
namespace {

// "https:", "mailto:" and any other URI scheme of two or more characters.
bool has_scheme(std::string_view target) {
    auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    if (target.empty() || !alpha(target[0])) return false;
    for (std::size_t i = 1; i < target.size(); ++i) {
        const char c = target[i];
        if (c == ':') return i >= 2;
        if (!alpha(c) && !(c >= '0' && c <= '9') && c != '+' && c != '.' && c != '-') return false;
    }
    return false;
}

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percent_decode(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

}  // namespace

std::optional<MarkdownLink> ReadOnlyNav::next_link(const MarkdownOutline& outline, std::uint64_t pos, int direction) {
    const auto& links = outline.links;
    if (links.empty()) return std::nullopt;
    if (direction >= 0) {
        for (const MarkdownLink& l : links)
            if (l.start > pos) return l;
        return links.front();
    }
    for (auto it = links.rbegin(); it != links.rend(); ++it)
        if (it->start < pos) return *it;
    return links.back();
}

std::optional<MarkdownLink> ReadOnlyNav::link_at(const MarkdownOutline& outline, std::uint64_t pos) {
    for (const MarkdownLink& l : outline.links)
        if (pos >= l.start && pos < l.end) return l;
    return std::nullopt;
}

std::optional<std::uint64_t> ReadOnlyNav::find_anchor(const MarkdownOutline& outline, std::string_view slug) {
    for (const MarkdownHeading& h : outline.headings)
        if (h.slug == slug) return h.start;
    return std::nullopt;
}

LinkAction ReadOnlyNav::resolve(const MarkdownLink& link, const std::filesystem::path& shown, const MarkdownOutline& here) {
    LinkAction a;
    const std::string_view target = link.target;
    if (has_scheme(target)) {
        a.kind = LinkAction::message;
        a.text = std::string(target) + " (web addresses are not opened)";
        return a;
    }
    const std::size_t hash = target.find('#');
    const std::string file = percent_decode(target.substr(0, hash));
    const std::string anchor = hash == std::string_view::npos ? std::string() : percent_decode(target.substr(hash + 1));
    if (file.empty()) {
        if (const auto at = find_anchor(here, anchor)) {
            a.kind = LinkAction::jump;
            a.offset = *at;
        } else {
            a.kind = LinkAction::message;
            a.text = "no heading #" + anchor + " in " + shown.filename().string();
        }
        return a;
    }
    const std::filesystem::path p(file);
    a.kind = LinkAction::open;
    a.path = (p.is_absolute() ? p : shown.parent_path() / p).lexically_normal();
    a.anchor = anchor;
    return a;
}

void ReadOnlyNav::reset(TrailEntry base) {
    trail_.clear();
    trail_.push_back(std::move(base));
    index_ = 0;
}

void ReadOnlyNav::visit(TrailEntry current, TrailEntry next) {
    trail_[index_] = std::move(current);
    trail_.resize(index_ + 1);
    trail_.push_back(std::move(next));
    index_ = trail_.size() - 1;
}

std::optional<TrailEntry> ReadOnlyNav::back(TrailEntry current) {
    if (index_ == 0) return std::nullopt;
    trail_[index_] = std::move(current);
    return trail_[--index_];
}

std::optional<TrailEntry> ReadOnlyNav::forward(TrailEntry current) {
    if (index_ + 1 >= trail_.size()) return std::nullopt;
    trail_[index_] = std::move(current);
    return trail_[++index_];
}

}  // namespace mod
