#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "search/search.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"

namespace mod {

enum class PromptKind { find, replace, goto_line, setting, color, prune_age, info };

// The bottom input bar and its modal overlays. Main thread.
class Prompt {
public:
    enum class Outcome { consumed, closed };
    // Returns an error to show inline (the prompt stays open), or nullopt to close.
    using SubmitFn = std::function<std::optional<std::string>(const std::string&)>;
    using CursorFn = std::function<std::uint64_t()>;

    explicit Prompt(Searcher* searcher);

    void open(PromptKind kind, std::string label, std::string initial_text, SubmitFn on_submit);
    // With `allow_replace` false (a read-only view), Tab does not switch to replace.
    void open_find(std::string initial_text, std::uint64_t origin, CursorFn cursor, bool allow_replace = true);
    void open_info(std::string title, std::string_view text);
    void set_searcher(Searcher* searcher);
    void close();

    bool is_open() const noexcept { return open_; }
    PromptKind kind() const noexcept { return kind_; }
    Outcome handle_event(const InputEvent& event);
    // Rows the open prompt needs above the status line on a `screen_rows` × `screen_cols` screen.
    int rows_wanted(int screen_rows, int screen_cols) const;
    void render(Screen& screen, Rect area) const;

    // For tests and the status line.
    const std::string& field(int index) const { return fields_[static_cast<std::size_t>(index)].text; }
    // The find bar's last query, whatever prompt was opened since.
    const std::string& last_query() const {
        return kind_ == PromptKind::find || kind_ == PromptKind::replace ? fields_[0].text : last_query_;
    }
    const std::string& error() const noexcept { return error_; }
    const SearchOptions& search_options() const noexcept { return options_; }

private:
    struct Field {
        std::string text;
        std::size_t cursor = 0;  // byte offset
    };

    void reset(PromptKind kind);
    bool edit_field(Field& f, const KeyEvent& key);
    void insert(Field& f, std::string_view bytes);
    void search_changed();
    void submit();
    Outcome handle_key(const KeyEvent& key);
    Outcome handle_find_key(const KeyEvent& key);

    Searcher* searcher_;
    bool open_ = false;
    std::uint64_t generation_ = 0;  // increases on every open
    PromptKind kind_ = PromptKind::find;
    std::string last_query_;  // kept while another kind of prompt is open or was last
    std::string label_;
    Field fields_[2];
    int active_ = 0;
    std::string error_;
    SubmitFn on_submit_;
    // find / replace
    SearchOptions options_;
    std::uint64_t origin_ = 0;
    CursorFn cursor_;
    bool allow_replace_ = true;
    // info
    std::vector<std::string> lines_;
    int scroll_ = 0;
};

}  // namespace mod
