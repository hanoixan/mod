#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "ui/input.hpp"
#include "ui/screen.hpp"

namespace mod {

// A one-line text input. Main thread.
class TextField {
public:
    explicit TextField(std::string initial = {});

    // True when the key edited or moved inside the field. Left at the start and Right at
    // the end return false, so that a dialog can move its focus.
    bool handle_key(const KeyEvent& key);
    // Pasted text, with line breaks removed.
    void insert(std::string_view bytes);

    const std::string& text() const noexcept { return text_; }
    void set_text(std::string text);
    bool at_start() const noexcept { return cursor_ == 0; }
    bool at_end() const noexcept { return cursor_ == text_.size(); }

    void render(Screen& screen, int row, int col, int width, Attr attr, bool focused);

private:
    std::size_t next_cluster(std::size_t pos) const;
    std::size_t prev_cluster(std::size_t pos) const;

    std::string text_;
    std::size_t cursor_ = 0;  // byte offset, on a cluster boundary
    int scroll_ = 0;          // display columns hidden on the left
};

}  // namespace mod
