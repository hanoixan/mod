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

// edit.py's Open and Save As dialog. Keyboard only. Main thread.
class FileDialog {
public:
    // `clock` returns seconds; it is the test seam for the type-to-search timeout.
    // Dot-files are shown unless `show_hidden` is false; the Hidden toggle changes it.
    FileDialog(FileDialogMode mode, std::filesystem::path initial_dir, std::string initial_filename,
               std::function<double()> clock = {}, bool show_hidden = true);

    FileDialogResult handle_key(const KeyEvent& key, int list_rows);
    void handle_paste(std::string_view bytes);
    void render(Screen& screen, Rect area);

    static int list_rows_for(int body_rows) { return body_rows - 8 < 1 ? 1 : body_rows - 8; }

    const std::filesystem::path& current_dir() const noexcept { return dir_; }
    const std::string& filename() const noexcept { return filename_.text(); }
    FocusZone focus() const noexcept { return focus_; }
    int selected() const noexcept { return selected_; }
    int scroll() const noexcept { return scroll_; }
    const std::vector<DirEntry>& entries() const noexcept { return entries_; }
    const std::string& error() const noexcept { return error_; }
    const std::string& search_text() const noexcept { return search_text_; }
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
    void search_char(char32_t ch, int list_rows);
    void jump_to_match(bool forward, bool from_current, int list_rows);
    void move_focus(int delta);
    FileDialogResult submit(const std::string& name);
    FileDialogResult activate(FocusZone zone);
    void leave_edit_modes();

    FileDialogMode mode_;
    std::function<double()> clock_;
    std::filesystem::path dir_;
    std::vector<DirEntry> entries_;
    std::string filter_ = "*.*";
    bool show_hidden_ = false;
    SortColumn sort_col_ = SortColumn::name;
    bool sort_asc_ = true;
    int selected_ = -1;
    int scroll_ = 0;
    FocusZone focus_ = FocusZone::list;
    TextField filename_;
    std::optional<TextField> filter_field_;
    std::optional<TextField> folder_field_;
    std::string error_;
    std::string search_text_;
    double search_time_ = 0;
};

}  // namespace mod
