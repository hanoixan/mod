#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "edit/document.hpp"
#include "edit/editor.hpp"
#include "search/regex.hpp"
#include "util/error.hpp"

namespace mod {

struct SearchOptions {
    bool regex = true;
    bool case_insensitive = false;
    bool whole_word = false;
    bool wrap = true;
};

enum class Direction { forward, backward };

struct StepResult {
    enum Kind { idle, in_progress, found, not_found, canceled, replaced } kind = idle;
    Match match;                  // for `found`
    std::uint64_t count = 0;      // for `replaced`: how many matches were replaced
    bool wrapped = false;         // this result crossed the end (or start) of the document
    bool line_too_long = false;   // a line longer than the window cap was searched only partly
    std::string message;          // why a job ended early ("pattern too complex")

    static StepResult of(Kind k) {
        StepResult r;
        r.kind = k;
        return r;
    }
};

inline constexpr std::uint64_t kStepBudget = 8ull << 20;

// Find and replace over the whole document, streamed in windows and time-sliced on
// the main thread.
class Searcher {
public:
    Searcher(Document& doc, Editor& editor) : doc_(doc), editor_(editor) {}
    ~Searcher() { cancel(); }
    Searcher(const Searcher&) = delete;
    Searcher& operator=(const Searcher&) = delete;

    Status set_query(std::string_view find_text, const SearchOptions& options);
    bool has_query() const noexcept { return regex_.has_value(); }

    void find_next(std::uint64_t from, Direction direction);
    StepResult step(std::uint64_t budget_bytes = kStepBudget);
    void cancel();

    Status replace_current(std::string_view replacement);
    Status replace_all(std::string_view replacement);

    std::optional<Match> last_match() const;
    bool active() const noexcept { return job_.has_value(); }

private:
    struct Job {
        enum Kind { find, replace_all } kind = find;
        Direction direction = Direction::forward;
        std::uint64_t origin = 0;   // where the lap started
        std::uint64_t pos = 0;      // forward: next offset to search from; backward: match-start limit
        std::uint64_t seg_end = 0;  // forward: matches must start before this (or at it, in the first segment)
        bool second_lap = false;    // wrapped
        bool reported_wrap = false;
        std::uint64_t version = 0;
        std::string replacement;
        std::uint64_t count = 0;
        bool too_long = false;
        std::optional<std::uint64_t> skip_empty_at;  // the previous empty match, stepped over
        bool mid_line = false;  // `pos` continues a line longer than the window cap
    };
    struct Scan {
        std::optional<Match> first;
        std::vector<Match> all;
        std::uint64_t next = 0;
        std::uint64_t window_start = 0;
        bool segment_done = false;
        bool truncated = false;  // `next` is inside a line longer than the cap
    };

    Result<Scan> scan_forward(std::uint64_t pos, bool mid_line, std::uint64_t seg_end, bool inclusive_end, bool all,
                              std::optional<std::uint64_t> skip_empty_at, bool& too_long);
    Result<std::optional<Match>> scan_backward(std::uint64_t limit, std::uint64_t floor, std::uint64_t& next_limit,
                                               bool& too_long);
    std::uint64_t read_window(std::uint64_t start, std::uint64_t length);
    std::uint64_t cp_length_at(std::uint64_t window_start, std::uint64_t offset) const;
    StepResult finish(StepResult r);

    Document& doc_;
    Editor& editor_;
    std::optional<Regex> regex_;
    SearchOptions options_;
    std::optional<Job> job_;
    std::optional<Match> last_match_;
    std::uint64_t last_version_ = 0;
    std::string window_;  // reused window buffer, copied out of the pieces
};

}  // namespace mod
