#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "app/folder_tree.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"

namespace mod {

enum class TreeKey { none, moved, preview, open, back, leave };

// What a key in the folder tree asks App to do: `preview` and `open` carry the file's path;
// `back` (Shift+Right) returns the keys to the views in escape mode, `leave` (Esc) to the
// view for editing.
struct TreeKeyResult {
    TreeKey kind = TreeKey::none;
    std::filesystem::path path;
};

// The folder tree's panel: its rows, the selected one kept in view and panned sideways so its
// whole name shows, and its own bottom line for its keys or the tree's message. Main thread.
class FolderTreeView {
public:
    static constexpr std::string_view kHints = "Space: preview  Enter: open  Shift+Right: back";
    static constexpr std::string_view kShortHints = "Space: view  Enter: open";  // when the full ones do not fit

    void set_tree(FolderTree* tree) noexcept { tree_ = tree; }
    // A message for the panel's own line until the next key (a file that cannot be opened).
    void set_message(std::string message) { message_ = std::move(message); }
    TreeKeyResult handle_key(const KeyEvent& key);
    // Draws the tree in `area`, its last row being the panel's own line; the selection is
    // highlighted while `focused`.
    void render(Screen& screen, Rect area, bool focused);

private:
    FolderTree* tree_ = nullptr;
    std::size_t top_ = 0;     // the first row shown
    int hscroll_ = 0;         // columns panned
    int list_rows_ = 1;       // the rows shown at the last render, a page
    std::string message_;     // App's, shown before the tree's own
};

}  // namespace mod
