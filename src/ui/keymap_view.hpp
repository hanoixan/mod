#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/commands.hpp"
#include "app/keymap.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"

namespace mod {

struct KeymapKeyResult {
    bool closed = false;     // Esc closed the panel
    bool changed = false;    // the keymap was edited: save it
    bool reset_all = false;  // the user asked to reset everything: confirm, then `reset_all()`
    std::string message;     // for the status line
};

// The Key Bindings editor: every bindable command with its keys, a search field that
// filters as you type, and key capture to add a binding. Main thread.
class KeymapView {
public:
    void open(Keymap& keymap);
    void close();
    bool is_open() const noexcept { return keymap_ != nullptr; }

    KeymapKeyResult handle_key(const KeyEvent& key);
    // Pasted text goes into the search field, up to its first line break.
    void handle_paste(std::string_view bytes);
    // Every command back to its default keys; the caller has asked the user.
    void reset_all();
    void render(Screen& screen, Rect area);

    bool capturing() const noexcept { return capturing_; }
    const std::string& search() const noexcept { return search_; }
    std::size_t row_count() const noexcept { return rows_.size(); }
    std::size_t selected() const noexcept { return selected_; }
    std::optional<CommandId> selected_command() const;
    // The row as drawn, without the selection marker: the name, the keys, and a
    // trailing `*` when they differ from the defaults.
    std::string row_text(std::size_t index) const;

private:
    void filter();
    KeymapKeyResult capture(const KeyEvent& key);

    Keymap* keymap_ = nullptr;  // while open
    std::string search_;
    std::vector<CommandId> rows_;  // the commands that match the search
    std::size_t selected_ = 0;
    std::size_t scroll_ = 0;
    bool capturing_ = false;
};

}  // namespace mod
