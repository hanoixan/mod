#include "ui/confirm_bar.hpp"

#include <algorithm>
#include <cctype>

#include "text/utf8.hpp"
#include "ui/theme.hpp"

namespace mod {
namespace {


}  // namespace

void ConfirmBar::open(std::string question, std::vector<std::string> choices, int esc_choice, ChoiceFn on_choice) {
    question_ = std::move(question);
    choices_ = std::move(choices);
    esc_choice_ = std::clamp(esc_choice, 0, std::max(0, static_cast<int>(choices_.size()) - 1));
    on_choice_ = std::move(on_choice);
    focused_ = 0;
    open_ = true;
}

void ConfirmBar::choose(int index) {
    open_ = false;
    ChoiceFn fn = std::move(on_choice_);
    on_choice_ = nullptr;
    if (fn) fn(index);
}

void ConfirmBar::handle_key(const KeyEvent& key) {
    if (!open_ || choices_.empty()) return;
    const int n = static_cast<int>(choices_.size());
    switch (key.key) {
        case Key::Tab:
        case Key::Right: focused_ = (focused_ + 1) % n; break;
        case Key::BackTab:
        case Key::Left: focused_ = (focused_ + n - 1) % n; break;
        case Key::Enter: choose(focused_); break;
        case Key::Escape: choose(esc_choice_); break;
        case Key::Char: {
            if ((key.mods & (kAlt | kCtrl)) || key.ch >= 0x80) break;
            const int c = std::tolower(static_cast<int>(key.ch));
            for (int i = 0; i < n; ++i) {
                const std::string& label = choices_[static_cast<std::size_t>(i)];
                if (!label.empty() && std::tolower(static_cast<unsigned char>(label[0])) == c) {
                    choose(i);
                    return;
                }
            }
            break;
        }
        default: break;  // consumed
    }
}

std::vector<std::string> ConfirmBar::question_lines(int cols) const {
    const int width = std::max(10, cols - 2);
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i < question_.size()) {
        const std::size_t sp = question_.find(' ', i);
        const std::string word = question_.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        i = sp == std::string::npos ? question_.size() : sp + 1;
        if (!line.empty() && text_columns(line) + 1 + text_columns(word) > width) {
            lines.push_back(std::move(line));
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
    }
    if (!line.empty() || lines.empty()) lines.push_back(std::move(line));
    return lines;
}

int ConfirmBar::rows(int cols) const { return open_ ? 2 + static_cast<int>(question_lines(cols).size()) : 0; }

void ConfirmBar::render(Screen& screen, Rect area) const {
    if (!open_ || area.rows <= 0) return;
    const Attr plain = attr_for(Style::Default);
    const Attr selected = attr_for(Style::list_selected);
    const int right = area.col + area.cols;
    const int bottom = area.row + area.rows;
    int row = area.row;
    screen.fill(row, area.col, right, plain);
    for (int c = area.col; c < right; ++c) screen.put(row, c, "─", 1, attr_for(Style::gutter));
    for (const std::string& line : question_lines(area.cols)) {
        if (++row >= bottom - 1) break;
        screen.fill(row, area.col, right, plain);
        screen.print(row, area.col + 1, right, line, plain);
    }
    row = bottom - 1;
    screen.fill(row, area.col, right, plain);
    int col = area.col + 1;
    for (std::size_t i = 0; i < choices_.size(); ++i) {
        const bool focus = static_cast<int>(i) == focused_;
        const Attr a = focus ? selected : plain;
        Attr accel = a;
        accel.flags |= kUnderline;
        if (i > 0) col = screen.print(row, col, right, "  ", plain);
        col = screen.print(row, col, right, focus ? "[>" : "[", a);
        const std::string& label = choices_[i];
        if (!label.empty()) {
            col = screen.print(row, col, right, label.substr(0, 1), accel);
            col = screen.print(row, col, right, label.substr(1), a);
        }
        col = screen.print(row, col, right, focus ? "<]" : "]", a);
    }
    screen.set_cursor(0, 0, false);
}

}  // namespace mod
