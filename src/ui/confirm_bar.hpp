#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/input.hpp"
#include "ui/screen.hpp"

namespace mod {

// A question with buttons above the status line: Tab and the arrows move between the
// buttons, Enter or a button's first letter chooses, Esc chooses the caller's Esc choice.
// Every key is consumed while it is open. Main thread.
class ConfirmBar {
public:
    using ChoiceFn = std::function<void(int)>;

    // Opening while open replaces the question; the first button has the focus.
    void open(std::string question, std::vector<std::string> choices, int esc_choice, ChoiceFn on_choice);
    bool is_open() const noexcept { return open_; }
    void close() { open_ = false; }

    // The bar is closed before the callback runs, so the callback can ask again.
    void handle_key(const KeyEvent& key);

    // The rule, the question's lines at `cols` wide, and the buttons; 0 when closed.
    int rows(int cols) const;
    void render(Screen& screen, Rect area) const;

    int focused() const noexcept { return focused_; }

private:
    std::vector<std::string> question_lines(int cols) const;
    void choose(int index);

    bool open_ = false;
    std::string question_;
    std::vector<std::string> choices_;
    int esc_choice_ = 0;
    int focused_ = 0;
    ChoiceFn on_choice_;
};

}  // namespace mod
