#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/error.hpp"

namespace mod {

struct RegexOptions {
    bool case_insensitive = false;
    bool literal = false;  // plain text, no regex syntax
    bool whole_word = false;
};

// Absolute document offsets. `groups[i]` is capture group i + 1; a group that did not
// take part in the match is `{kUnsetGroup, kUnsetGroup}`, so numbering is preserved.
struct Match {
    static constexpr std::uint64_t kUnsetGroup = std::numeric_limits<std::uint64_t>::max();

    std::uint64_t start = 0;
    std::uint64_t end = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> groups;

    friend bool operator==(const Match&, const Match&) = default;
};

// Where the line holding the last `from` began and ended, so a caller searching one window
// again and again, from ever later points, finds each line's bounds once instead of
// rescanning a long line for every match.
struct LineCursor {
    bool valid = false;           // set by the first search
    std::size_t searched_to = 0;  // window index of the last `from`
    std::size_t line = 0;         // the start of its line
    std::size_t line_feed = 0;    // that line's LF, or the window size when it has none
};

struct WindowResult {
    enum Kind { found, need_more, none } kind = none;
    Match match;
};

// A compiled PCRE2 pattern, matched line by line over a window of the document.
// No PCRE2 type appears here. Move-only; main thread.
class Regex {
public:
    // Fetches the document bytes in [start, end).
    using ReadBytes = std::function<std::string(std::uint64_t start, std::uint64_t end)>;

    static Result<Regex> compile(std::string_view pattern, const RegexOptions& options);

    Regex(Regex&&) noexcept;
    Regex& operator=(Regex&&) noexcept;
    ~Regex();

    // `window` starts at a line start at absolute offset `window_offset`. Errors are
    // `regex` ("pattern too complex" when the match budget is exceeded).
    Result<WindowResult> search_window(std::span<const std::byte> window, std::uint64_t window_offset,
                                       std::uint64_t from, bool at_eof, LineCursor* cursor = nullptr) const;

    // Expands `$n`, `${n}`, `${name}`, `$0` and `$$`; literal patterns return the template.
    Result<std::string> expand_replacement(const Match& match, std::string_view template_,
                                           const ReadBytes& read) const;

    std::uint32_t group_count() const noexcept;
    bool literal() const noexcept;

private:
    struct Impl;
    explicit Regex(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace mod
