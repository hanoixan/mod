#include "ui/list_cursor.hpp"

#include <algorithm>

namespace mod {

std::optional<ListStep> list_step(Key key, std::size_t selected, std::size_t count) {
    const std::size_t last = count == 0 ? 0 : count - 1;
    switch (key) {
        case Key::Up: return ListStep{selected > 0 ? selected - 1 : 0, -1};
        case Key::Down: return ListStep{std::min(last, selected + 1), 1};
        case Key::Home: return ListStep{0, 1};
        case Key::End: return ListStep{last, -1};
        case Key::PageUp: return ListStep{selected > kListPage ? selected - kListPage : 0, -1};
        case Key::PageDown: return ListStep{std::min(last, selected + kListPage), 1};
        default: return std::nullopt;
    }
}

std::size_t scroll_to_show(std::size_t selected, std::size_t scroll, std::size_t visible) {
    visible = std::max<std::size_t>(1, visible);
    if (selected < scroll) return selected;
    if (selected >= scroll + visible) return selected + 1 - visible;
    return scroll;
}

}  // namespace mod
