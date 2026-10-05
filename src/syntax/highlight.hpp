#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "edit/document.hpp"

namespace mod {

// Semantic text classes; the theme maps them to terminal attributes.
enum class Style : std::uint8_t {
    Default,
    // Markdown.
    md_heading1,
    md_heading2,
    md_heading3,
    md_heading4,
    md_heading5,
    md_heading6,
    md_emphasis,
    md_strong,
    md_strike,
    md_code,
    md_code_block,
    md_link_text,
    md_link_url,
    md_quote,
    md_list_marker,
    md_markup,
    // LSP standard token types, in the order of the specification.
    lsp_namespace,
    lsp_type,
    lsp_class,
    lsp_enum,
    lsp_interface,
    lsp_struct,
    lsp_type_parameter,
    lsp_parameter,
    lsp_variable,
    lsp_property,
    lsp_enum_member,
    lsp_event,
    lsp_function,
    lsp_method,
    lsp_macro,
    lsp_keyword,
    lsp_modifier,
    lsp_comment,
    lsp_string,
    lsp_number,
    lsp_regexp,
    lsp_operator,
    lsp_decorator,
    // The syntax layer.
    constant,
    // UI.
    search_match,
    selection,
    gutter,
    gutter_current,
    status,
    menu,
    menu_selected,
    menu_accel,
    error,
    history_read_only,
    page,  // the text area's background, which darkness sets
    history_inserted,  // the Undo History preview: text the selected step inserted
    history_removed,   // ...and text it removed
    overflow_marker,
};

// StyleSpan::modifiers bits: the LSP modifiers the theme renders.
inline constexpr std::uint8_t kModDeprecated = 1u << 0;
inline constexpr std::uint8_t kModReadonly = 1u << 1;
inline constexpr std::uint8_t kModDocumentation = 1u << 2;
inline constexpr std::uint8_t kModDefaultLibrary = 1u << 3;
inline constexpr std::uint8_t kModDeclaration = 1u << 4;

// Absolute byte offsets, half-open. Spans for one line are sorted and do not overlap.
struct StyleSpan {
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    Style style = Style::Default;
    std::uint8_t modifiers = 0;

    friend bool operator==(const StyleSpan&, const StyleSpan&) = default;
};

// Anything that colors a document. Main thread only; never throws into the render
// path. The owner registers it with Document::add_listener.
class Highlighter : public DocumentListener {
public:
    using Clock = std::chrono::steady_clock;

    // `line_bytes` is the line without its LF, possibly truncated. Never blocks on I/O.
    virtual std::vector<StyleSpan> spans_for_line(std::uint64_t line_start, std::string_view line_bytes) = 0;

    // After scrolling; lets asynchronous highlighters prefetch.
    virtual void visible_range_changed(std::uint64_t /*first_offset*/, std::uint64_t /*last_offset*/) {}

    // Runs timers; returns the next deadline, if any.
    virtual std::optional<Clock::time_point> tick(Clock::time_point /*now*/) { return std::nullopt; }

    // Short state text for the status line; empty when there is nothing to show.
    virtual std::string status() const { return {}; }
};

}  // namespace mod
