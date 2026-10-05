#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ui/input.hpp"
#include "ui/screen.hpp"
#include "ui/theme.hpp"

namespace mod {

// What a key in the Colors editor asks App to do; the editor never changes the theme.
struct ColorsKeyResult {
    bool closed = false;                // Esc closed the panel
    std::optional<std::string> edit;    // prompt for this name's spec
    std::optional<std::string> reset;   // give this name its default
    bool reset_all = false;             // confirm, then reset everything
};

// The Colors editor: one row per color name under its group's heading, each with a
// sample in its look, its spec, and `*` when changed. Main thread.
class ColorsView {
public:
    void open(const ColorTheme& theme);
    void close();
    bool is_open() const noexcept { return theme_ != nullptr; }

    ColorsKeyResult handle_key(const KeyEvent& key);
    void render(Screen& screen, Rect area);

    std::size_t row_count() const { return rows_.size(); }
    std::size_t selected() const noexcept { return selected_; }
    std::string selected_name() const;
    std::string row_text(std::size_t index) const;

private:
    struct Row {
        std::string_view heading;            // set for a heading row
        const ColorEntry* entry = nullptr;  // set for an entry row
    };

    // The entry row nearest `from` in direction `step` (+1 or -1), else the other way.
    std::size_t entry_near(std::size_t from, int step) const;

    const ColorTheme* theme_ = nullptr;
    std::vector<Row> rows_;
    std::size_t name_width_ = 0;
    std::size_t selected_ = 0;
    std::size_t scroll_ = 0;
};

}  // namespace mod
