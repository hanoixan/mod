#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "syntax/highlight.hpp"
#include "syntax/language_config.hpp"

namespace mod {

// What carries from one line to the next: inside a block comment or a multi-line
// string, by its index in the spec, or neither (-1).
struct SyntaxState {
    int block_comment = -1;
    int string = -1;
    friend bool operator==(const SyntaxState&, const SyntaxState&) = default;
};

// One scanned line: spans relative to the line, and the state at its end.
struct SyntaxLine {
    std::vector<StyleSpan> spans;
    SyntaxState end;
};

class SyntaxRules;

// The syntax layer: keywords, constants, strings, comments and numbers, from a
// language's SyntaxSpec. Knows no language itself. Main thread.
class SyntaxHighlighter final : public Highlighter {
public:
    // The state is assumed plain past this many bytes before the nearest checkpoint.
    static constexpr std::uint64_t kMaxBackScan = std::uint64_t{4} << 20;
    static constexpr std::uint64_t kCheckpointLines = 256;

    SyntaxHighlighter(const Document& doc, SyntaxSpec spec);
    ~SyntaxHighlighter() override;

    // Scans one line from `state`. Pure, for tests and for the built-in data's checks.
    static SyntaxLine scan_line(const SyntaxSpec& spec, SyntaxState state, std::string_view line);

    std::vector<StyleSpan> spans_for_line(std::uint64_t line_start, std::string_view line_bytes) override;

    void after_change(const ChangeEvent& ev) override;
    void reloaded() override;

private:
    struct Checkpoint {
        SyntaxState state;
        bool trusted = true;  // false when it derives from an assumed plain state
    };
    struct LineState {
        SyntaxState state;
        bool trusted = true;
        std::uint64_t lines_since_checkpoint = 0;
    };
    LineState state_at(std::uint64_t line_start);

    const Document& doc_;
    std::unique_ptr<const SyntaxRules> rules_;
    std::map<std::uint64_t, Checkpoint> checkpoints_;
    std::optional<std::pair<std::uint64_t, LineState>> next_line_;  // the line after the last one colored
};

}  // namespace mod
