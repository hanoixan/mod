#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "app/commands.hpp"
#include "app/keymap.hpp"
#include "app/settings.hpp"
#include "platform/terminal.hpp"
#include "ui/menu.hpp"
#include "ui/screen.hpp"
#include "ui/theme.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

class NullTerminal : public Terminal {
public:
    Status enter_raw_mode() override { return {}; }
    void restore() noexcept override {}
    TerminalSize size() override { return {}; }
    Result<std::size_t> read_input(std::span<std::byte>) override { return 0; }
    Status write(std::span<const std::byte>) override { return {}; }
    WaitEvents wait(int) override { return timed_out; }
    void wake() noexcept override {}
};

KeyEvent key(Key k) { return KeyEvent{k, 0, 0}; }
KeyEvent ch(char32_t c) { return KeyEvent{Key::Char, c, 0}; }

constexpr auto kOptions = static_cast<std::size_t>(MenuId::options);

std::string screen_row(const Screen& s, int row) {
    std::string out;
    for (int c = 0; c < s.cols(); ++c) {
        const Cell& cell = s.cell(row, c);
        out.append(cell.utf8.data(), cell.len);
    }
    return out;
}

// Settings that are never written, with a clock that advances a second per change.
struct Fixture {
    std::int64_t now = 1'790'958'600'000;
    Settings settings{fs::path(MOD_TEST_SCRATCH) / "menu_test" / "never_written",
                      [](const fs::path&, const ContentProducer&) -> Status { return {}; },
                      [this] { return now += 1000; }};
    Keymap keymap;
    MenuBar menu{keymap};
    Fixture() { settings.load(); }
    const std::vector<MenuItem>& options() const { return menu.menus()[kOptions].items; }
    // The row of an open menu's item `i` when the bar is drawn on row 0: menus open downward.
    static int item_row(std::size_t i) { return 1 + static_cast<int>(i); }
};

}  // namespace

TEST_CASE("Options holds User Settings…, Key Bindings… and Colors…; Tab Width… is no longer a fixed item") {
    Fixture f;
    for (int pass = 0; pass < 2; ++pass) {  // as built, and after a rebuild with nothing recent
        REQUIRE(f.menu.menus().size() == 6);
        CHECK(f.menu.menus()[kOptions].title == "Options");
        REQUIRE(f.options().size() == 4);
        CHECK(f.options()[0].label == "User Settings…");
        CHECK(f.options()[0].command == CommandId::UserSettings);
        CHECK(f.options()[1].label.empty());  // a separator
        CHECK(f.options()[2].label == "Key Bindings…");
        CHECK(f.options()[2].command == CommandId::KeyBindings);
        CHECK(f.options()[3].label == "Colors…");
        CHECK(f.options()[3].command == CommandId::Colors);
        CHECK(f.options()[3].accel == 'l');
        f.menu.set_recent_settings(f.settings);
    }
}

TEST_CASE("Help holds Documentation (F1) and About, and no longer the key bindings") {
    Fixture f;
    const auto& help = f.menu.menus()[5];
    CHECK(help.title == "Help");
    REQUIRE(help.items.size() == 2);
    CHECK(help.items[0].command == CommandId::ShowHelp);
    CHECK(help.items[0].label == "Documentation");
    CHECK(help.items[0].accel == 'd');
    CHECK(help.items[1].command == CommandId::About);
}

TEST_CASE("the recently changed settings are listed under User Settings…, newest first") {
    Fixture f;
    REQUIRE(f.settings.set("tab_width", 8));
    REQUIRE(f.settings.set("line_numbers", 0));
    f.menu.set_recent_settings(f.settings);
    REQUIRE(f.options().size() == 6);
    CHECK(f.options()[0].command == CommandId::UserSettings);
    CHECK(f.options()[3].label.empty());
    CHECK(f.options()[4].command == CommandId::KeyBindings);
    CHECK(f.options()[5].command == CommandId::Colors);
    CHECK(f.options()[1].label == "1 Line numbers on open");
    CHECK(f.options()[1].accel == '1');
    CHECK(f.options()[1].command == CommandId::RecentSetting1);
    CHECK(f.options()[1].checkable);
    CHECK(f.options()[2].label == "2 Tab width: 8…");
    CHECK(f.options()[2].accel == '2');
    CHECK(f.options()[2].command == CommandId::RecentSetting2);
    CHECK_FALSE(f.options()[2].checkable);

    REQUIRE(f.settings.set("tab_width", 2));  // changed again: it moves to the top
    f.menu.set_recent_settings(f.settings);
    CHECK(f.options()[1].label == "1 Tab width: 2…");
    CHECK(f.options()[2].label == "2 Line numbers on open");
}

TEST_CASE("at most five recent settings are listed") {
    Fixture f;
    std::size_t changed = 0;
    for (const SettingSpec& spec : setting_specs()) {
        if (spec.type == SettingType::keymap || spec.type == SettingType::colors) continue;  // own editors, never recent
        REQUIRE(f.settings.set(spec.key, spec.def == spec.min ? spec.max : spec.min));
        ++changed;
    }
    f.menu.set_recent_settings(f.settings);
    CHECK(f.options().size() == 1 + std::min<std::size_t>(5, changed) + 3);
    CHECK(recent_setting_command(0) == CommandId::RecentSetting1);
    CHECK(recent_setting_command(4) == CommandId::RecentSetting5);
    CHECK(recent_setting_index(CommandId::RecentSetting3) == 2);
    CHECK_FALSE(recent_setting_index(CommandId::UserSettings).has_value());
}

TEST_CASE("a digit picks a recent setting; Enter on the first item opens User Settings") {
    Fixture f;
    REQUIRE(f.settings.set("tab_width", 8));
    REQUIRE(f.settings.set("line_numbers", 0));
    f.menu.set_recent_settings(f.settings);
    f.menu.open(MenuId::options);
    auto cmd = f.menu.handle_key(ch(U'2'));
    REQUIRE(cmd.has_value());
    CHECK(*cmd == CommandId::RecentSetting2);
    CHECK(f.menu.flashing());  // chosen by its letter: it runs after the flash
    f.menu.open(MenuId::options);
    cmd = f.menu.handle_key(key(Key::Enter));
    REQUIRE(cmd.has_value());
    CHECK(*cmd == CommandId::UserSettings);
    f.menu.open(MenuId::options);
    cmd = f.menu.handle_key(ch(U'u'));
    REQUIRE(cmd.has_value());
    CHECK(*cmd == CommandId::UserSettings);
    f.menu.open(MenuId::options);
    cmd = f.menu.handle_key(ch(U'k'));
    REQUIRE(cmd.has_value());
    CHECK(*cmd == CommandId::KeyBindings);
    // Up and Down step over the separator; End lands on the last item.
    f.menu.open(MenuId::options);
    f.menu.handle_key(key(Key::End));
    CHECK(f.menu.item_index() == 5);  // Colors…
    f.menu.handle_key(key(Key::Up));
    CHECK(f.menu.item_index() == 4);
    f.menu.handle_key(key(Key::Up));
    CHECK(f.menu.item_index() == 2);
    f.menu.handle_key(key(Key::Down));
    CHECK(f.menu.item_index() == 4);
    f.menu.open(MenuId::options);
    cmd = f.menu.handle_key(ch(U'l'));
    REQUIRE(cmd.has_value());
    CHECK(*cmd == CommandId::Colors);
}

TEST_CASE("a menu shows the key a command is bound to now, not its default") {
    Fixture f;
    NullTerminal term;
    Screen screen(term);
    screen.resize({10, 60});
    f.menu.open(MenuId::file);
    f.menu.render(screen, 0, {});
    const int save = f.item_row(1);  // File > Save
    CHECK(screen_row(screen, save).find("Ctrl+S") != std::string::npos);
    REQUIRE(f.keymap.unbind(CommandId::Save, KeyEvent{Key::CtrlLetter, U's', kCtrl}));
    REQUIRE(f.keymap.bind(CommandId::Save, KeyEvent{Key::F2, 0, 0}).kind == BindOutcome::bound);
    f.menu.render(screen, 0, {});
    CHECK(screen_row(screen, save).find("Ctrl+S") == std::string::npos);
    CHECK(screen_row(screen, save).find("F2") != std::string::npos);
}

TEST_CASE("render: a recent on/off setting shows a check mark when it is on") {
    Fixture f;
    REQUIRE(f.settings.set("line_numbers", 0));
    f.menu.set_recent_settings(f.settings);
    NullTerminal term;
    Screen screen(term);
    screen.resize({10, 60});
    f.menu.open(MenuId::options);
    f.menu.render(screen, 0, [](CommandId id) { return id == CommandId::RecentSetting1; });
    CHECK(screen_row(screen, f.item_row(0)).find("User Settings…") != std::string::npos);
    const int recent = f.item_row(1);
    CHECK(screen_row(screen, recent).find("✓ 1 Line numbers on open") != std::string::npos);
    f.menu.render(screen, 0, [](CommandId) { return false; });
    CHECK(screen_row(screen, recent).find("✓") == std::string::npos);
    CHECK(screen_row(screen, recent).find("1 Line numbers on open") != std::string::npos);
}

TEST_CASE("View has a checkable Word Wrap item") {
    Fixture f;
    const auto& view = f.menu.menus()[2];
    CHECK(view.title == "View");
    bool found = false;
    for (const MenuItem& item : view.items) {
        if (item.command != CommandId::ToggleWordWrap) continue;
        found = true;
        CHECK(item.label == "Word Wrap");
        CHECK(item.accel == 'w');
        CHECK(item.checkable);
    }
    CHECK(found);
    f.menu.open(MenuId::view);
    const auto cmd = f.menu.handle_key(ch(U'w'));
    REQUIRE(cmd.has_value());
    CHECK(*cmd == CommandId::ToggleWordWrap);
}

TEST_CASE("File no longer holds Clear History… or Trim History…; the Undo History pane does") {
    Fixture f;
    const auto& file = f.menu.menus()[0];
    CHECK(file.title == "File");
    std::vector<CommandId> commands;
    for (const MenuItem& item : file.items) commands.push_back(item.command);
    CHECK(commands == std::vector<CommandId>{CommandId::Open, CommandId::Save, CommandId::SaveAs, CommandId::CloseDocument, CommandId::Suspend, CommandId::Exit});
}

TEST_CASE("File > Suspend shows Ctrl+T; Edit > Cut to Line End shows Ctrl+K") {
    Fixture f;
    NullTerminal term;
    Screen screen(term);
    screen.resize({20, 60});
    f.menu.open(MenuId::file);
    f.menu.render(screen, 0, {});
    CHECK(screen_row(screen, f.item_row(4)).find("Suspend") != std::string::npos);
    CHECK(screen_row(screen, f.item_row(4)).find("Ctrl+T") != std::string::npos);
    const auto& edit = f.menu.menus()[1];
    bool found = false;
    for (std::size_t i = 0; i < edit.items.size(); ++i) {
        if (edit.items[i].command != CommandId::CutToLineEnd) continue;
        found = true;
        CHECK(edit.items[i].label == "Cut to Line End");
        CHECK(edit.items[i].accel == 'l');
        CHECK(i > 0);
        CHECK(edit.items[i - 1].command == CommandId::Cut);  // right after Cut
    }
    CHECK(found);
}

TEST_CASE("View has a checkable Read Only item") {
    Fixture f;
    const auto& view = f.menu.menus()[2];
    bool found = false;
    for (const MenuItem& item : view.items) {
        if (item.command != CommandId::ToggleReadOnly) continue;
        found = true;
        CHECK(item.label == "Read Only");
        CHECK(item.accel == 'r');
        CHECK(item.checkable);
    }
    CHECK(found);
}

TEST_CASE("the Documents menu sits after View and lists the open documents in opening order") {
    Fixture f;
    REQUIRE(f.menu.menus().size() == 6);
    const auto& docs = f.menu.menus()[3];
    CHECK(docs.title == "Documents");
    CHECK(docs.accel == 'd');
    f.menu.set_documents({{"guide.md", false, false}, {"notes.txt", true, true}, {"guide.md \u2192 keys.md", false, false}});
    const auto& items = f.menu.menus()[3].items;
    REQUIRE(items.size() == 3);
    CHECK(items[0].label == "1 guide.md");
    CHECK(items[0].accel == '1');
    CHECK(items[1].label == "2 notes.txt *");  // unsaved changes
    CHECK(items[2].label == "3 guide.md \u2192 keys.md");
    for (std::size_t i = 0; i < items.size(); ++i) {
        CHECK(items[i].command == CommandId::ShowDocument);
        CHECK(items[i].arg == static_cast<int>(i));
        CHECK(items[i].checkable);
    }
    CHECK(items[1].checked == true);
    CHECK(items[0].checked == false);
}

TEST_CASE("choosing a document: a digit or Enter gives ShowDocument and its position") {
    Fixture f;
    f.menu.set_documents({{"a", false, true}, {"b", false, false}, {"c", false, false}});
    f.menu.open(MenuId::documents);
    auto cmd = f.menu.handle_key(ch(U'3'));
    REQUIRE(cmd.has_value());
    CHECK(*cmd == CommandId::ShowDocument);
    CHECK(f.menu.chosen_arg() == 2);
    f.menu.open(MenuId::documents);
    f.menu.handle_key(key(Key::Down));
    cmd = f.menu.handle_key(key(Key::Enter));
    REQUIRE(cmd.has_value());
    CHECK(f.menu.chosen_arg() == 1);
}

TEST_CASE("past nine documents the items have no digit; the check comes from the item, not the callback") {
    Fixture f;
    std::vector<DocumentMenuEntry> many;
    for (int i = 0; i < 11; ++i) many.push_back({"f" + std::to_string(i), false, i == 10});
    f.menu.set_documents(many);
    const auto& items = f.menu.menus()[3].items;
    REQUIRE(items.size() == 11);
    CHECK(items[8].accel == '9');
    CHECK(items[9].accel == 0);
    CHECK(items[9].label == "f9");
    NullTerminal term;
    Screen screen(term);
    screen.resize({16, 60});
    f.menu.open(MenuId::documents);
    f.menu.render(screen, 0, [](CommandId) { return true; });  // would check every item if it were asked
    CHECK(screen_row(screen, f.item_row(10)).find("✓ f10") != std::string::npos);
    CHECK(screen_row(screen, f.item_row(0)).find("✓") == std::string::npos);
}

TEST_CASE("File has Close") {
    Fixture f;
    const auto& file = f.menu.menus()[0].items;
    bool close = false;
    for (const MenuItem& item : file) close = close || (item.command == CommandId::CloseDocument && item.label == "Close" && item.accel == 'c');
    CHECK(close);
}

TEST_CASE("the bar is hidden until shown; F10 and Alt+X show it; no other Alt+letter menu keys remain") {
    Fixture f;
    CHECK_FALSE(f.menu.is_visible());
    CHECK_FALSE(f.menu.is_open());
    CHECK(f.keymap.lookup(KeyEvent{Key::Char, U'x', kAlt}) == CommandId::ShowMenu);
    CHECK(f.keymap.lookup(key(Key::F10)) == CommandId::ShowMenu);
    for (char32_t c : {U'f', U'e', U'v', U'd', U'o', U'h', U'm'}) CHECK_FALSE(f.keymap.lookup(KeyEvent{Key::Char, c, kAlt}).has_value());
    CHECK(f.keymap.binding_label(CommandId::OpenMenuFile).empty());
    // Hidden, the bar draws nothing.
    NullTerminal term;
    Screen screen(term);
    screen.resize({10, 60});
    f.menu.render(screen, 0, {});
    for (int r = 0; r < 10; ++r) CHECK(screen_row(screen, r).find_first_not_of(" ") == std::string::npos);
}

TEST_CASE("shown, the bar is armed: a menu's letter opens it and an item's letter runs it") {
    Fixture f;
    f.menu.show();
    CHECK(f.menu.is_visible());
    CHECK_FALSE(f.menu.is_open());
    NullTerminal term;
    Screen screen(term);
    screen.resize({10, 60});
    f.menu.render(screen, 0, {});
    CHECK(screen_row(screen, 0).find("File") != std::string::npos);  // at the top
    CHECK(screen_row(screen, 1).find_first_not_of(" ") == std::string::npos);  // no menu yet
    CHECK_FALSE(f.menu.handle_key(ch(U'f')).has_value());
    CHECK(f.menu.is_open());
    CHECK(f.menu.menu_index() == 0);
    f.menu.render(screen, 0, {});
    const int exit_row = Fixture::item_row(f.menu.menus()[0].items.size() - 1);  // the File menu's last item
    CHECK(screen_row(screen, exit_row).find("Exit") != std::string::npos);
    const auto cmd = f.menu.handle_key(ch(U'x'));  // Esc F X
    REQUIRE(cmd.has_value());
    CHECK(*cmd == CommandId::Exit);
    CHECK(f.menu.flashing());  // shown chosen for a moment; App runs it when the flash ends
    CHECK(f.menu.is_visible());
    f.menu.render(screen, 0, {});
    const int exit_col = static_cast<int>(screen_row(screen, exit_row).find("Exit"));
    CHECK(screen.cell(exit_row, exit_col).attr == attr_for(Style::menu_selected));
    f.menu.hide();
    CHECK_FALSE(f.menu.flashing());
    CHECK_FALSE(f.menu.is_visible());
    f.menu.show();
    CHECK(f.menu.handle_key(key(Key::Enter)) == std::nullopt);  // Enter opens the highlighted menu
    CHECK_FALSE(f.menu.flashing());
    f.menu.handle_key(key(Key::Up));
    CHECK(f.menu.handle_key(key(Key::Enter)) == CommandId::Exit);  // Enter runs at once, no flash
    CHECK_FALSE(f.menu.flashing());
    CHECK_FALSE(f.menu.is_visible());
    f.menu.show();
    f.menu.handle_key(ch(U'H'));  // letters in any case
    CHECK(f.menu.menu_index() == 5);
}

TEST_CASE("armed: Left and Right move across the titles, Enter or Down opens, Esc hides") {
    Fixture f;
    f.menu.show();
    f.menu.handle_key(key(Key::Right));
    f.menu.handle_key(key(Key::Right));
    CHECK_FALSE(f.menu.is_open());
    CHECK(f.menu.menu_index() == 2);
    f.menu.handle_key(key(Key::Enter));
    CHECK(f.menu.is_open());
    CHECK(f.menu.menu_index() == 2);
    f.menu.hide();
    f.menu.show();
    f.menu.handle_key(key(Key::Left));  // wraps to Help
    f.menu.handle_key(key(Key::Down));
    CHECK(f.menu.is_open());
    CHECK(f.menu.menu_index() == 5);
    f.menu.hide();
    f.menu.show();
    f.menu.handle_key(ch(U'z'));  // no such menu: consumed, still armed
    CHECK(f.menu.is_visible());
    CHECK_FALSE(f.menu.is_open());
    f.menu.handle_key(key(Key::Escape));
    CHECK_FALSE(f.menu.is_visible());
}

TEST_CASE("open: Esc hides the whole bar, and Alt+letter no longer switches menus") {
    Fixture f;
    f.menu.show();
    f.menu.handle_key(ch(U'e'));
    CHECK_FALSE(f.menu.handle_key(KeyEvent{Key::Char, U'v', kAlt}).has_value());
    CHECK(f.menu.menu_index() == 1);
    CHECK(f.menu.is_open());
    f.menu.handle_key(key(Key::Escape));
    CHECK_FALSE(f.menu.is_visible());
    f.menu.open(MenuId::documents);  // the OpenMenu commands open one menu directly
    CHECK(f.menu.is_visible());
    CHECK(f.menu.is_open());
    CHECK(f.menu.menu_index() == 3);
}

TEST_CASE("the idle hint names Esc, which always opens the menu") {
    Fixture f;
    CHECK(help_hint(f.keymap) == "Esc h: help");
    REQUIRE(f.keymap.unbind(CommandId::ShowMenu, key(Key::F10)));
    CHECK(help_hint(f.keymap) == "Esc h: help");
}

TEST_CASE("a recent choice setting shows its name, without the ellipsis of a prompt") {
    Fixture f;
    REQUIRE(f.settings.set("terminal_mode", 1));
    f.menu.set_recent_settings(f.settings);
    CHECK(f.options()[1].label == "1 Terminal mode: vt100");
    CHECK_FALSE(f.options()[1].checkable);
}

TEST_CASE("View has no Markdown Formatting item: Syntax Coloring covers Markdown") {
    Fixture f;
    const auto& view = f.menu.menus()[2].items;
    std::vector<std::string> labels;
    for (const MenuItem& item : view) labels.push_back(item.label);
    CHECK(labels == std::vector<std::string>{"Line Numbers", "Syntax Coloring", "Word Wrap", "Read Only", "Pin Folder Tree", "", "Split", "Unsplit"});
    CHECK_FALSE(command_by_name("ToggleMarkdown").has_value());
}

TEST_CASE("View holds Split (p) and Unsplit (u) after a separator") {
    Fixture f;
    const auto& view = f.menu.menus()[2].items;
    REQUIRE(view.size() >= 3);
    CHECK(view[view.size() - 2].command == CommandId::Split);
    CHECK(view[view.size() - 2].accel == 'p');
    CHECK(view.back().command == CommandId::Unsplit);
    CHECK(view.back().accel == 'u');
}
