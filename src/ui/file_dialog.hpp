#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app/file_listing.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"
#include "ui/text_field.hpp"

namespace mod {

enum class FileDialogMode { open, save };

struct FileDialogResult {
    enum class Kind { pending, chosen, canceled } kind = Kind::pending;
    std::filesystem::path path;  // the full path, for `chosen`
};

// The focus zones, in Tab order.
enum class FocusZone { up, home, folder, hidden, filter, sort_name, sort_size, sort_modified, list, filename, submit, cancel };

// edit.py's Open and Save As dialog: the keyboard, and the mouse through click and scroll. Main thread.
class FileDialog {
public:
    // Dot-files are shown unless `show_hidden` is false; the Hidden toggle changes it. The
    // File field has the focus.
    FileDialog(FileDialogMode mode, std::filesystem::path initial_dir, std::string initial_filename, bool show_hidden = true);

    FileDialogResult handle_key(const KeyEvent& key, int list_rows);
    void handle_paste(std::string_view bytes);
    void render(Screen& screen, Rect area);
    // A mouse press at screen (`row`, `col`), as the last render drew the dialog: an entry is
    // selected, and opened (as Enter) on a double click; a button or header acts as Enter on it;
    // a part of the path goes to that folder; the File field takes the focus.
    FileDialogResult click(int row, int col, bool double_click, int list_rows);
    // The mouse wheel: the selection `delta` entries down (up when negative).
    void wheel(int delta, int list_rows);

    static int list_rows_for(int body_rows) { return body_rows - 8 < 1 ? 1 : body_rows - 8; }

    const std::filesystem::path& current_dir() const noexcept { return dir_; }
    const std::string& filename() const noexcept { return filename_.text(); }
    FocusZone focus() const noexcept { return focus_; }
    int selected() const noexcept { return selected_; }
    int scroll() const noexcept { return scroll_; }
    const std::vector<DirEntry>& entries() const noexcept { return entries_; }
    const std::string& error() const noexcept { return error_; }
    // The entry the File field's text found in the list (see search_field), or empty.
    const std::string& match() const noexcept { return match_name_; }
    // The lighter text drawn after the File field's: the rest of the match's name when the
    // name starts with the text, " → " and the whole name when it only contains it; empty when
    // there is no match, it is the text itself, or the field's cursor is not at its end.
    std::string completion() const;
    const std::string& filter() const noexcept { return filter_; }
    bool show_hidden() const noexcept { return show_hidden_; }
    bool editing_filter() const noexcept { return filter_field_.has_value(); }
    bool editing_folder() const noexcept { return folder_field_.has_value(); }

private:
    void refresh();
    void navigate_to(const std::filesystem::path& path);
    void select(int index, int list_rows);
    void update_filename_from_selection();
    void ensure_visible(int list_rows);
    // Looks the File field's text up in the list: the first entry whose name starts with it,
    // else the first that contains it, both ignoring case, is selected and scrolled to, and
    // the field keeps the text. Text with a path separator is a path, not looked up.
    void search_field(int list_rows);
    void jump_to_match(bool forward, bool from_current, int list_rows);
    void move_focus(int delta);
    FileDialogResult submit(const std::string& name);
    FileDialogResult activate(FocusZone zone);
    void leave_edit_modes();
    void handle_list_step(bool up, int list_rows);
    struct DrawnZone {
        int row = 0;
        int from = 0;  // [from, to) columns
        int to = 0;
        FocusZone zone = FocusZone::list;
    };
    std::vector<DrawnZone> drawn_zones_;
    struct DrawnCrumb {
        int row = 0;
        int from = 0;  // [from, to) columns
        int to = 0;
        int up = 0;  // how many folders above the current one it names
    };
    std::vector<DrawnCrumb> drawn_crumbs_;
    int drawn_list_row_ = 0;
    int drawn_list_rows_ = 0;

    FileDialogMode mode_;
    std::filesystem::path dir_;
    std::vector<DirEntry> entries_;
    std::string filter_ = "*.*";
    bool show_hidden_ = false;
    SortColumn sort_col_ = SortColumn::name;
    bool sort_asc_ = true;
    int selected_ = -1;
    int scroll_ = 0;
    FocusZone focus_ = FocusZone::filename;
    TextField filename_;
    std::optional<TextField> filter_field_;
    std::optional<TextField> folder_field_;
    std::string error_;
    std::string match_name_;   // the entry search_field found, until the selection moves
    bool match_prefix_ = false;  // its name starts with the field's text
};

}  // namespace mod
