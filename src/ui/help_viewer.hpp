#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "syntax/markdown_render.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"
#include "util/error.hpp"

namespace mod {

struct HelpKeyResult {
    bool closed = false;  // Esc closed the viewer
    bool search = false;  // `/` or Ctrl+F: open the help's search panel
    std::string message;  // for the status line, or empty
};

// The help viewer: the manual laid out for reading, with Lynx-style keys. Its page,
// place and back stack outlive closing it, for the session. Main thread.
class HelpViewer {
public:
    // Opens on the page it was left on, or the first time on `root`/index.md.
    Status open(const std::filesystem::path& root);
    void close() { open_ = false; }
    bool is_open() const noexcept { return open_; }

    // Shows `page` (relative to the root) with its 1-based `source_line` at the top.
    Status show(const std::filesystem::path& page, std::uint64_t source_line);

    HelpKeyResult handle_key(const KeyEvent& key);
    bool used() const noexcept { return used_; }  // whether the last key meant something here

    void render(Screen& screen, Rect area);

    const std::filesystem::path& page() const noexcept { return page_; }
    std::size_t top() const noexcept { return top_; }
    std::optional<std::string> selected_target() const;

private:
    struct Place {
        std::filesystem::path page;
        std::uint32_t source_top = 1;
        std::optional<std::size_t> link;
    };

    Status load(const std::filesystem::path& page);
    void relayout();
    std::size_t line_for_source(std::uint32_t source_line) const;
    std::size_t max_top() const;
    bool link_visible(std::size_t link) const;
    std::optional<std::size_t> first_visible_link() const;
    void keep_selection_visible();
    void scroll_to(std::size_t top);
    std::string follow(const std::string& target);
    void back();
    Place here() const;

    std::filesystem::path root_;
    std::filesystem::path page_;  // relative to the root
    std::string text_;
    RenderedPage rendered_;
    int width_ = 80;
    int rows_ = 24;
    std::size_t top_ = 0;
    std::optional<std::size_t> selected_;
    std::vector<Place> back_;
    bool open_ = false;
    bool used_ = false;
};

}  // namespace mod
