#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "syntax/highlight.hpp"
#include "syntax/language_config.hpp"
#include "syntax/lsp_client.hpp"
#include "syntax/lsp_pool.hpp"

namespace mod {

// Coloring from LSP semantic tokens. Keeps its own line-start index of the document
// (which is under the LSP size cap) for LSP positions, and caches decoded tokens as
// absolute byte spans that are shifted on edits until fresh tokens arrive.
// The style for a legend token type: its own when standard, else that of the
// standard type its name ends with (without case, the longest winning), else Default.
Style style_for_token_type(std::string_view name);

// The modifier bit for a legend modifier name, 0 for one the theme does not render.
std::uint8_t modifier_bit(std::string_view name);

class SemanticHighlighter final : public Highlighter {
public:
    static constexpr std::chrono::milliseconds kDebounce{150};
    static constexpr std::uint64_t kRangeMarginLines = 100;

    SemanticHighlighter(const Document& doc, const LanguageServerSpec& spec, LspServerPool& servers);
    ~SemanticHighlighter() override;

    std::vector<StyleSpan> spans_for_line(std::uint64_t line_start, std::string_view line_bytes) override;
    void visible_range_changed(std::uint64_t first_offset, std::uint64_t last_offset) override;
    std::optional<Clock::time_point> tick(Clock::time_point now) override;
    std::string status() const override;

    void before_change(const ChangeEvent& ev) override;
    void after_change(const ChangeEvent& ev) override;
    void reloaded() override;
    void saved() override;

private:
    void build_line_index();
    std::uint64_t line_of(std::uint64_t offset) const;
    std::uint64_t line_end(std::uint64_t line) const;  // excluding the LF
    LspPosition position_of(std::uint64_t offset) const;
    void build_legend_map();
    void request();
    void on_tokens(TokenResponse r);

    const Document& doc_;
    std::shared_ptr<LspClient> client_;  // shared with the other documents of its language and project
    LspClient::DocumentId doc_id_ = 0;
    std::string status_;  // when there is no client

    std::vector<std::uint64_t> line_starts_;  // line_starts_[0] == 0
    std::vector<StyleSpan> spans_;            // sorted, non-overlapping, absolute offsets
    std::vector<Style> type_styles_;          // by legend type index
    std::vector<std::uint8_t> modifier_bits_; // by legend modifier index

    std::optional<std::pair<LspPosition, LspPosition>> pending_change_;  // from before_change
    std::optional<LineRange> visible_;
    bool debounce_reset_ = false;
    // Changes since the last tick; past kBurstChanges the document is resynchronized whole.
    static constexpr std::size_t kBurstChanges = 64;
    std::size_t changes_since_tick_ = 0;
    bool burst_ = false;
    std::optional<Clock::time_point> deadline_;
};

}  // namespace mod
