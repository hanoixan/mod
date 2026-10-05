#include "ui/file_dialog.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <span>

#include "text/utf8.hpp"
#include "ui/theme.hpp"

namespace fs = std::filesystem;

namespace mod {
namespace {

constexpr FocusZone kZones[] = {FocusZone::up,        FocusZone::home,      FocusZone::folder,        FocusZone::hidden,
                                FocusZone::filter,    FocusZone::sort_name, FocusZone::sort_size,     FocusZone::sort_modified,
                                FocusZone::list,      FocusZone::filename,  FocusZone::submit,        FocusZone::cancel};
constexpr int kZoneCount = static_cast<int>(sizeof kZones / sizeof kZones[0]);
constexpr double kSearchTimeout = 2.0;

int zone_index(FocusZone z) {
    for (int i = 0; i < kZoneCount; ++i)
        if (kZones[i] == z) return i;
    return 0;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string strip(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

int text_cols(std::string_view s) {
    int cols = 0;
    for (std::size_t i = 0; i < s.size();) {
        const Decoded d = decode(std::as_bytes(std::span(s.data() + i, s.size() - i)));
        cols += std::max(0, display_width(d, static_cast<std::uint64_t>(cols), 1));
        i += std::max<std::size_t>(1, d.len);
    }
    return cols;
}

// The longest prefix of `s` that fits in `width` columns.
std::string fit(std::string_view s, int width) {
    int cols = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const Decoded d = decode(std::as_bytes(std::span(s.data() + i, s.size() - i)));
        const int w = std::max(0, display_width(d, static_cast<std::uint64_t>(cols), 1));
        if (cols + w > width) break;
        cols += w;
        i += std::max<std::size_t>(1, d.len);
    }
    return std::string(s.substr(0, i));
}

void print_right(Screen& screen, int row, int end_col, std::string_view text, Attr attr, int width_limit) {
    const std::string t = fit(text, width_limit);
    const int w = text_cols(t);
    screen.print(row, end_col - w, end_col, t, attr);
}

}  // namespace

FileDialog::FileDialog(FileDialogMode mode, fs::path initial_dir, std::string initial_filename, std::function<double()> clock,
                       bool show_hidden)
    : mode_(mode), clock_(std::move(clock)), dir_(std::move(initial_dir)), show_hidden_(show_hidden), filename_(std::move(initial_filename)) {
    if (!clock_) {
        clock_ = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    }
    dir_ = fs::absolute(dir_).lexically_normal();
    refresh();
}

void FileDialog::refresh() {
    error_.clear();
    auto listing = list_directory(dir_, filter_, show_hidden_);
    if (listing) {
        entries_ = std::move(*listing);
        sort_entries(entries_, sort_col_, sort_asc_);
    } else {
        entries_.clear();
        error_ = listing.error().message;
    }
    selected_ = -1;
    scroll_ = 0;
    folder_field_.reset();
}

void FileDialog::navigate_to(const fs::path& path) {
    std::error_code ec;
    const fs::path abs = fs::absolute(path).lexically_normal();
    if (!fs::is_directory(abs, ec)) return;
    dir_ = abs.has_filename() ? abs : abs.parent_path();
    if (dir_.empty()) dir_ = "/";
    refresh();
}

void FileDialog::update_filename_from_selection() {
    if (selected_ >= 0 && selected_ < static_cast<int>(entries_.size()) && !entries_[static_cast<std::size_t>(selected_)].is_dir)
        filename_.set_text(entries_[static_cast<std::size_t>(selected_)].name);
}

void FileDialog::ensure_visible(int list_rows) {
    if (selected_ < 0) return;
    if (selected_ < scroll_) scroll_ = selected_;
    else if (selected_ >= scroll_ + list_rows) scroll_ = selected_ - list_rows + 1;
    scroll_ = std::max(0, scroll_);
}

void FileDialog::select(int index, int list_rows) {
    selected_ = index;
    update_filename_from_selection();
    ensure_visible(list_rows);
}

void FileDialog::jump_to_match(bool forward, bool from_current, int list_rows) {
    const int n = static_cast<int>(entries_.size());
    if (search_text_.empty() || n == 0) return;
    int start;
    if (from_current) start = selected_ >= 0 ? ((selected_ + (forward ? 1 : -1)) % n + n) % n : 0;
    else start = selected_ >= 0 ? selected_ : 0;
    const int step = forward ? 1 : -1;
    for (int k = 0; k < n; ++k) {
        const int idx = (((start + k * step) % n) + n) % n;
        if (lower(entries_[static_cast<std::size_t>(idx)].name).find(search_text_) != std::string::npos) {
            select(idx, list_rows);
            return;
        }
    }
}

void FileDialog::search_char(char32_t ch, int list_rows) {
    const double now = clock_();
    if (now - search_time_ > kSearchTimeout) search_text_.clear();
    search_text_ += lower(to_utf8(ch));
    search_time_ = now;
    jump_to_match(true, false, list_rows);
}

void FileDialog::move_focus(int delta) {
    leave_edit_modes();
    focus_ = kZones[(zone_index(focus_) + delta + kZoneCount) % kZoneCount];
}

void FileDialog::leave_edit_modes() {
    filter_field_.reset();
    folder_field_.reset();
}

FileDialogResult FileDialog::submit(const std::string& name) {
    if (name.empty()) return {};
    const fs::path full = dir_ / name;
    std::error_code ec;
    if (fs::is_directory(full, ec)) {
        navigate_to(full);
        return {};
    }
    return {FileDialogResult::Kind::chosen, full};
}

FileDialogResult FileDialog::activate(FocusZone zone) {
    switch (zone) {
        case FocusZone::up: navigate_to(dir_.parent_path()); break;
        case FocusZone::home:
            if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') navigate_to(home);
            break;
        case FocusZone::folder: folder_field_.emplace(); break;
        case FocusZone::hidden:
            show_hidden_ = !show_hidden_;
            refresh();
            break;
        case FocusZone::filter: filter_field_.emplace(filter_); break;
        case FocusZone::sort_name:
        case FocusZone::sort_size:
        case FocusZone::sort_modified: {
            const SortColumn col = zone == FocusZone::sort_name ? SortColumn::name : zone == FocusZone::sort_size ? SortColumn::size : SortColumn::modified;
            if (sort_col_ == col) {
                sort_asc_ = !sort_asc_;
            } else {
                sort_col_ = col;
                sort_asc_ = true;
            }
            sort_entries(entries_, sort_col_, sort_asc_);
            break;
        }
        case FocusZone::submit: return submit(filename_.text());
        case FocusZone::cancel: return {FileDialogResult::Kind::canceled, {}};
        default: break;
    }
    return {};
}

FileDialogResult FileDialog::handle_key(const KeyEvent& key, int list_rows) {
    list_rows = std::max(1, list_rows);
    const bool shift = (key.mods & kShift) != 0;

    // The two small edit modes of the toolbar.
    if (folder_field_ || filter_field_) {
        TextField& field = folder_field_ ? *folder_field_ : *filter_field_;
        if (key.key == Key::Tab || key.key == Key::BackTab) {
            move_focus(key.key == Key::Tab ? 1 : -1);
            return {};
        }
        if (key.key == Key::Escape) {
            leave_edit_modes();
            return {};
        }
        if (key.key == Key::Enter) {
            const std::string text = strip(field.text());
            if (folder_field_) {
                folder_field_.reset();
                if (!text.empty()) {
                    refresh();
                    if (auto s = make_directory(dir_ / text); !s) error_ = s.error().message;
                    else refresh();
                }
            } else {
                filter_field_.reset();
                if (!text.empty()) filter_ = text;
                refresh();
            }
            return {};
        }
        field.handle_key(key);
        return {};
    }

    if (key.key == Key::Tab || key.key == Key::BackTab) {
        move_focus(key.key == Key::Tab && !shift ? 1 : -1);
        return {};
    }
    if (key.key == Key::Escape) return {FileDialogResult::Kind::canceled, {}};

    const int n = static_cast<int>(entries_.size());
    if (focus_ == FocusZone::list) {
        switch (key.key) {
            case Key::Left:
                if (!shift) move_focus(-1);
                return {};
            case Key::Right:
                if (!shift) move_focus(1);
                return {};
            case Key::Up:
                if (shift) {
                    jump_to_match(false, true, list_rows);
                } else {
                    if (selected_ > 0) --selected_;
                    else if (selected_ == -1 && n > 0) selected_ = 0;
                    update_filename_from_selection();
                    ensure_visible(list_rows);
                }
                return {};
            case Key::Down:
                if (shift) {
                    jump_to_match(true, true, list_rows);
                } else {
                    if (selected_ < n - 1) ++selected_;
                    update_filename_from_selection();
                    ensure_visible(list_rows);
                }
                return {};
            case Key::PageUp:
                select(selected_ <= 0 ? 0 : std::max(0, selected_ - list_rows), list_rows);
                return {};
            case Key::PageDown:
                if (n > 0) select(selected_ < 0 ? std::min(n - 1, list_rows - 1) : std::min(n - 1, selected_ + list_rows), list_rows);
                return {};
            case Key::Home:
                if (n > 0) select(0, list_rows);
                return {};
            case Key::End:
                if (n > 0) select(n - 1, list_rows);
                return {};
            case Key::Enter:
                if (selected_ >= 0 && selected_ < n) {
                    const DirEntry& e = entries_[static_cast<std::size_t>(selected_)];
                    if (e.is_dir) navigate_to(dir_ / e.name);
                    else return submit(e.name);
                } else if (!filename_.text().empty()) {
                    return submit(filename_.text());
                }
                return {};
            case Key::Backspace: navigate_to(dir_.parent_path()); return {};
            case Key::Char:
                if (!(key.mods & kCtrl)) search_char(key.ch, list_rows);
                return {};
            default: return {};
        }
    }
    if (focus_ == FocusZone::filename) {
        if (key.key == Key::Enter) return submit(filename_.text());
        if (key.key == Key::Left && filename_.at_start()) {
            move_focus(-1);
            return {};
        }
        if (key.key == Key::Right && filename_.at_end()) {
            move_focus(1);
            return {};
        }
        filename_.handle_key(key);
        return {};
    }
    // A button or a header.
    if (key.key == Key::Left) move_focus(-1);
    else if (key.key == Key::Right) move_focus(1);
    else if (key.key == Key::Enter) return activate(focus_);
    return {};
}

void FileDialog::handle_paste(std::string_view bytes) {
    if (folder_field_) folder_field_->insert(bytes);
    else if (filter_field_) filter_field_->insert(bytes);
    else if (focus_ == FocusZone::filename) filename_.insert(bytes);
}

void FileDialog::render(Screen& screen, Rect area) {
    const int W = area.cols;
    const int right = area.col + W;
    const int list_rows = list_rows_for(area.rows);
    int r = 0;
    auto row_ok = [&] { return r < area.rows; };
    auto begin_row = [&](Attr fill = attr_for(Style::Default)) {
        const int row = area.row + r;
        screen.fill(row, area.col, right, fill);
        return row;
    };
    auto zattr = [&](FocusZone z, Attr normal) { return focus_ == z ? attr_for(Style::list_selected) : normal; };
    bool cursor_placed = false;
    auto sep_row = [&](int row) {
        for (int c = area.col; c < right; ++c) screen.print(row, c, right, "─", attr_for(Style::gutter));
    };

    // 1. Title
    if (row_ok()) {
        const int row = begin_row();
        sep_row(row);
        screen.print(row, area.col + 2, right, mode_ == FileDialogMode::open ? " Open File " : " Save As ", attr_for(Style::gutter));
        ++r;
    }
    // 2. Breadcrumb
    if (row_ok()) {
        const int row = begin_row();
        int col = area.col + 1;
        const auto parts = path_parts(dir_);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) col = screen.print(row, col, right, " > ", attr_for(Style::gutter));
            col = screen.print(row, col, right, parts[i], attr_for(Style::md_link_text));
        }
        ++r;
    }
    // 3. Toolbar
    if (row_ok()) {
        const int row = begin_row();
        int col = area.col + 1;
        col = screen.print(row, col, right, "[↑Up]", zattr(FocusZone::up, attr_for(Style::Default)));
        col = screen.print(row, col + 1, right, "[⌂Home]", zattr(FocusZone::home, attr_for(Style::Default))) + 0;
        col += 1;
        if (folder_field_) {
            folder_field_->render(screen, row, col, 14, attr_for(Style::status), true);
            if (folder_field_->text().empty()) screen.print(row, col, col + 14, "folder name", attr_for(Style::status));
            cursor_placed = true;
        } else {
            screen.print(row, col, right, "[+Folder]", zattr(FocusZone::folder, attr_for(Style::Default)));
        }
        // Right side: Hidden and the filter.
        std::string filt = "[" + filter_ + "]";
        const int filt_w = filter_field_ ? 14 : text_cols(filt);
        const int filt_col = right - 1 - filt_w;
        if (filter_field_) {
            filter_field_->render(screen, row, filt_col, filt_w, attr_for(Style::status), true);
            cursor_placed = true;
        } else {
            screen.print(row, filt_col, right, filt, zattr(FocusZone::filter, attr_for(Style::Default)));
        }
        const std::string hidden = show_hidden_ ? "◉Hidden" : "○Hidden";
        screen.print(row, filt_col - 1 - text_cols(hidden), right, hidden, zattr(FocusZone::hidden, attr_for(Style::Default)));
        ++r;
    }
    // 4. Header
    const int wn = W * 55 / 100;
    const int ws = W * 20 / 100;
    const int wm = W * 20 / 100;
    const int size_end = area.col + 1 + wn + ws;
    const int mod_end = size_end + wm;
    auto arrow = [&](SortColumn c) { return sort_col_ == c ? (sort_asc_ ? " ▲" : " ▼") : ""; };
    if (row_ok()) {
        const int row = begin_row();
        screen.print(row, area.col + 1, right, std::string("Name") + arrow(SortColumn::name), zattr(FocusZone::sort_name, attr_for(Style::gutter)));
        print_right(screen, row, size_end, std::string("Size") + arrow(SortColumn::size), zattr(FocusZone::sort_size, attr_for(Style::gutter)), ws);
        print_right(screen, row, std::min(mod_end, right), std::string("Modified") + arrow(SortColumn::modified), zattr(FocusZone::sort_modified, attr_for(Style::gutter)), wm);
        ++r;
    }
    // 5. Separator
    if (row_ok()) {
        sep_row(begin_row());
        ++r;
    }
    // 6. List
    const int n = static_cast<int>(entries_.size());
    for (int i = 0; i < list_rows && row_ok(); ++i) {
        const int idx = scroll_ + i;
        const bool last_slot = i == list_rows - 1;
        if (last_slot && !error_.empty()) {
            const int row = begin_row(attr_for(Style::error));
            screen.print(row, area.col + 1, right, fit(error_, W - 2), attr_for(Style::error));
            ++r;
            continue;
        }
        const bool sel = idx == selected_ && idx < n;
        const int row = begin_row(sel ? attr_for(Style::list_selected) : attr_for(Style::Default));
        if (idx < n) {
            const DirEntry& e = entries_[static_cast<std::size_t>(idx)];
            const Attr base = sel ? attr_for(Style::list_selected) : attr_for(Style::Default);
            const Attr sec = sel ? attr_for(Style::list_selected) : attr_for(Style::gutter);
            screen.print(row, area.col + 1, right, e.is_dir ? "📁" : "📄", base);
            screen.print(row, area.col + 4, right, fit(e.name, std::max(1, wn - 3)), base);
            if (!e.is_dir) print_right(screen, row, size_end, format_size(e.size), sec, ws);
            print_right(screen, row, std::min(mod_end, right), e.modified.size() > 5 ? e.modified.substr(5) : e.modified, sec, wm);
        }
        ++r;
    }
    // 7. Separator
    if (row_ok()) {
        sep_row(begin_row());
        ++r;
    }
    // 8. File
    if (row_ok()) {
        const int row = begin_row();
        const int col = screen.print(row, area.col + 1, right, "File: ", attr_for(Style::Default));
        const bool focus = focus_ == FocusZone::filename;
        filename_.render(screen, row, col, std::max(1, right - col - 1), focus ? attr_for(Style::list_selected) : attr_for(Style::status), focus && !cursor_placed);
        if (focus && !cursor_placed) cursor_placed = true;
        ++r;
    }
    // 9. Actions
    if (row_ok()) {
        const int row = begin_row();
        int col = area.col + 1;
        col = screen.print(row, col, right, mode_ == FileDialogMode::open ? "[Open]" : "[Save]", zattr(FocusZone::submit, attr_for(Style::gutter_current)));
        screen.print(row, col + 1, right, "[Cancel]", zattr(FocusZone::cancel, attr_for(Style::Default)));
        std::string pos;
        if (n > 0 && selected_ >= 0) pos = std::format("{} of {}", selected_ + 1, n);
        else if (n > 0) pos = std::format("{} items", n);
        int end = right - 1;
        if (!pos.empty()) {
            print_right(screen, row, end, pos, attr_for(Style::gutter), W);
            end -= text_cols(pos) + 2;
        }
        if (!search_text_.empty()) print_right(screen, row, end, "⌕ " + search_text_, attr_for(Style::gutter_current), W);
        ++r;
    }
    if (!cursor_placed) screen.set_cursor(0, 0, false);
}

}  // namespace mod
