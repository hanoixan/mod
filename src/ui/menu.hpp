#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/commands.hpp"
#include "app/keymap.hpp"
#include "app/settings.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"

namespace mod {

// A separator is an item with an empty label.
// The menus of the bar, left to right.
enum class MenuId { file, edit, view, documents, options, help };

struct MenuItem {
    MenuItem() = default;  // a separator
    MenuItem(std::string label_, char accel_, CommandId command_, bool checkable_)
        : label(std::move(label_)), accel(accel_), command(command_), checkable(checkable_) {}

    std::string label;
    char accel = 0;  // lowercase
    CommandId command = CommandId::Exit;
    bool checkable = false;
    int arg = -1;                // which document, for ShowDocument
    std::optional<bool> checked; // set: the item's own check mark, not the render callback's
};

// One open document as the Documents menu shows it.
struct DocumentMenuEntry {
    std::string label;
    bool dirty = false;
    bool shown = false;
};

struct Menu {
    std::string title;
    char accel = 0;  // lowercase
    std::vector<MenuItem> items;
};

// The menu bar on row 0 and its drop-downs. Main thread.
// The status line's idle hint: how to reach the menu and its Help ("Esc h: help").
std::string help_hint(const Keymap& keymap);

class MenuBar {
public:
    explicit MenuBar(const Keymap& keymap);

    // Shows the bar and opens one menu.
    void open(MenuId menu);
    // Shows the hidden bar, armed: the next letter picks a menu.
    void show();
    void hide() { visible_ = open_ = flashing_ = false; }
    void close() { hide(); }
    bool is_visible() const noexcept { return visible_; }
    bool is_open() const noexcept { return open_; }
    // An item chosen by its letter is shown selected for a moment before it runs.
    bool flashing() const noexcept { return flashing_; }
    int menu_index() const noexcept { return menu_; }
    int item_index() const noexcept { return item_; }

    // The command to run (the menu has closed), or nullopt (navigation, consumed).
    std::optional<CommandId> handle_key(const KeyEvent& key);
    // The bar on `bar_row` (above the status line), its menu opening upward.
    void render(Screen& screen, int bar_row, const std::function<bool(CommandId)>& checked) const;
    // Rebuilds the Options menu: User Settings…, then the settings changed most recently
    // (at most five, newest first), numbered 1 to 5.
    void set_recent_settings(const Settings& settings);
    // Rebuilds the Documents menu: one item per entry, numbered 1 to 9 as accelerators.
    void set_documents(const std::vector<DocumentMenuEntry>& documents);
    // The `arg` of the item last chosen (ShowDocument's document position).
    int chosen_arg() const noexcept { return chosen_arg_; }

    const std::vector<Menu>& menus() const noexcept { return menus_; }

private:
    void open_at(int menu_index);  // clamped to the menus there are
    void move_item(int delta);

    const Keymap& keymap_;
    std::vector<Menu> menus_;
    bool visible_ = false;  // armed or open
    bool open_ = false;
    bool flashing_ = false;
    int menu_ = 0;
    int item_ = 0;
    int chosen_arg_ = -1;
};

}  // namespace mod
