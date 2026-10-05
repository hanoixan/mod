#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "app/settings.hpp"
#include "platform/terminal.hpp"
#include "ui/screen.hpp"
#include "ui/list_cursor.hpp"
#include "ui/settings_view.hpp"
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

std::string screen_row(const Screen& s, int row) {
    std::string out;
    for (int c = 0; c < s.cols(); ++c) {
        const Cell& cell = s.cell(row, c);
        out.append(cell.utf8.data(), cell.len);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::size_t index_of(std::string_view key_name) {
    const auto specs = setting_specs();
    for (std::size_t i = 0; i < specs.size(); ++i) {
        if (specs[i].key == key_name) return i;
    }
    FAIL("no such setting");
    return 0;
}

// Settings that are never written, with every value at its default.
struct Fixture {
    Settings settings{fs::path(MOD_TEST_SCRATCH) / "settings_view_test" / "never_written",
                      [](const fs::path&, const ContentProducer&) -> Status { return {}; }};
    SettingsView view;
    Fixture() {
        settings.load();
        view.open(settings);
    }
    void select(std::string_view key_name) {
        view.handle_key(key(Key::Home));
        for (std::size_t i = 0; i < index_of(key_name); ++i) view.handle_key(key(Key::Down));
    }
};

}  // namespace

TEST_CASE("the panel has one row for every setting in the schema, in schema order") {
    Fixture f;
    const auto specs = setting_specs();
    REQUIRE(f.view.row_count() == specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        CAPTURE(specs[i].key);
        const std::string row = f.view.row_text(i);
        CHECK(row.find(specs[i].label) != std::string::npos);
        if (specs[i].type == SettingType::boolean) {
            CHECK(row.ends_with(specs[i].def != 0 ? "[x]" : "[ ]"));
        } else if (specs[i].type == SettingType::integer) {
            CHECK(row.ends_with(std::to_string(specs[i].def)));
        } else if (specs[i].type == SettingType::choice) {
            CHECK(row.ends_with(specs[i].choices[static_cast<std::size_t>(specs[i].def)]));
        } else {
            CHECK(row.ends_with("defaults"));
        }
    }
}

TEST_CASE("rows show the current values, not the defaults") {
    Fixture f;
    REQUIRE(f.settings.set("tab_width", 8));
    REQUIRE(f.settings.set("line_numbers", 0));
    CHECK(f.view.row_text(index_of("tab_width")).ends_with("8"));
    CHECK(f.view.row_text(index_of("line_numbers")).ends_with("[ ]"));
}

TEST_CASE("open and close; Esc closes") {
    Settings settings(fs::path(MOD_TEST_SCRATCH) / "settings_view_test" / "x");
    SettingsView view;
    CHECK_FALSE(view.is_open());
    view.open(settings);
    CHECK(view.is_open());
    CHECK(view.selected() == 0);
    const SettingsKeyResult r = view.handle_key(key(Key::Escape));
    CHECK(r.closed);
    CHECK(r.change == nullptr);
    CHECK_FALSE(view.is_open());
}

TEST_CASE("Up, Down, Home, End and the page keys move the selection and stop at the ends") {
    Fixture f;
    const std::size_t last = setting_specs().size() - 1;
    f.view.handle_key(key(Key::Up));
    CHECK(f.view.selected() == 0);
    f.view.handle_key(key(Key::Down));
    CHECK(f.view.selected() == 1);
    f.view.handle_key(key(Key::End));
    CHECK(f.view.selected() == last);
    f.view.handle_key(key(Key::Down));
    CHECK(f.view.selected() == last);
    f.view.handle_key(key(Key::Home));
    CHECK(f.view.selected() == 0);
    f.view.handle_key(key(Key::PageDown));
    CHECK(f.view.selected() == std::min(last, kListPage));
    for (int i = 0; i < 5; ++i) f.view.handle_key(key(Key::PageDown));
    CHECK(f.view.selected() == last);  // pages stop at the end
    for (int i = 0; i < 5; ++i) f.view.handle_key(key(Key::PageUp));
    CHECK(f.view.selected() == 0);
}

TEST_CASE("Enter and Space ask for an on/off setting to be flipped") {
    Fixture f;
    f.select("line_numbers");
    SettingsKeyResult r = f.view.handle_key(key(Key::Enter));
    REQUIRE(r.change != nullptr);
    CHECK(r.change->key == "line_numbers");
    CHECK(r.value == 0);
    CHECK(r.edit == nullptr);
    CHECK_FALSE(r.closed);
    CHECK(f.settings.flag("line_numbers"));  // the view never changes Settings itself
    REQUIRE(f.settings.set("line_numbers", 0));
    r = f.view.handle_key(ch(U' '));
    REQUIRE(r.change != nullptr);
    CHECK(r.value == 1);
}

TEST_CASE("Enter on a number asks for a prompt; Left and Right step it within its range") {
    Fixture f;
    f.select("tab_width");
    SettingsKeyResult r = f.view.handle_key(key(Key::Enter));
    REQUIRE(r.edit != nullptr);
    CHECK(r.edit->key == "tab_width");
    CHECK(r.change == nullptr);
    r = f.view.handle_key(key(Key::Right));
    REQUIRE(r.change != nullptr);
    CHECK(r.value == 5);
    r = f.view.handle_key(key(Key::Left));
    REQUIRE(r.change != nullptr);
    CHECK(r.value == 3);
    REQUIRE(f.settings.set("tab_width", 16));
    CHECK(f.view.handle_key(key(Key::Right)).change == nullptr);  // at the top of the range
    REQUIRE(f.settings.set("tab_width", 1));
    CHECK(f.view.handle_key(key(Key::Left)).change == nullptr);
    CHECK(f.view.handle_key(ch(U' ')).change == nullptr);  // Space does nothing to a number
}

TEST_CASE("Left and Right do nothing to an on/off setting; other keys are consumed") {
    Fixture f;
    f.select("line_numbers");
    CHECK(f.view.handle_key(key(Key::Left)).change == nullptr);
    CHECK(f.view.handle_key(key(Key::Right)).change == nullptr);
    const SettingsKeyResult r = f.view.handle_key(ch(U'q'));
    CHECK(r.change == nullptr);
    CHECK(r.edit == nullptr);
    CHECK_FALSE(r.closed);
    CHECK(f.view.is_open());
}

TEST_CASE("render: the rows, the selected row highlighted, and its help text below") {
    Fixture f;
    NullTerminal term;
    Screen screen(term);
    screen.resize({20, 60});
    f.select("line_numbers");
    const Rect area{1, 1, 18, 58};
    f.view.render(screen, area);
    const std::size_t i = index_of("line_numbers");
    for (std::size_t r = 0; r < setting_specs().size(); ++r) {
        CHECK(screen_row(screen, 1 + static_cast<int>(r)).find(setting_specs()[r].label) != std::string::npos);
    }
    CHECK(screen.cell(1 + static_cast<int>(i), 1).attr == attr_for(Style::menu_selected));
    CHECK(screen.cell(1, 1).attr != attr_for(Style::menu_selected));
    std::string below;
    for (int r = 1 + static_cast<int>(setting_specs().size()); r < 19; ++r) below += screen_row(screen, r) + " ";
    CHECK(below.find("Show the line-number gutter") != std::string::npos);
    CHECK(below.find("document.") != std::string::npos);  // wrapped onto a third row, not cut
    CHECK(screen_row(screen, 0).empty());  // nothing is drawn outside the area
}

TEST_CASE("render: a short area scrolls to keep the selected row visible") {
    Fixture f;
    NullTerminal term;
    Screen screen(term);
    screen.resize({6, 50});
    const Rect area{1, 0, 2, 50};
    f.view.handle_key(key(Key::End));
    f.view.render(screen, area);
    const std::string last_label(setting_specs().back().label);
    CHECK((screen_row(screen, 1) + screen_row(screen, 2)).find(last_label) != std::string::npos);
    f.view.handle_key(key(Key::Home));
    f.view.render(screen, area);
    CHECK(screen_row(screen, 1).find(setting_specs().front().label) != std::string::npos);
}

TEST_CASE("the key bindings row shows how many commands were changed, and Enter asks for their editor") {
    Fixture f;
    const std::size_t i = index_of("keymap");
    CHECK(f.view.row_text(i).ends_with("defaults"));
    REQUIRE(f.settings.set_raw("keymap", *Json::parse(R"({"GotoLine": ["F5"], "Save": []})")));
    CHECK(f.view.row_text(i).ends_with("2 changed"));
    f.select("keymap");
    const SettingsKeyResult r = f.view.handle_key(key(Key::Enter));
    REQUIRE(r.edit != nullptr);
    CHECK(r.edit->type == SettingType::keymap);
    CHECK(r.change == nullptr);
    CHECK(f.view.handle_key(key(Key::Right)).change == nullptr);
    CHECK(f.view.handle_key(ch(U' ')).change == nullptr);
}

TEST_CASE("a setting added to the schema shows up with no change to the panel: word wrap") {
    Fixture f;
    const std::size_t i = index_of("word_wrap");
    CHECK(f.view.row_text(i).starts_with("Word wrap on open"));
    CHECK(f.view.row_text(i).ends_with("[x]"));  // on by default
    f.select("word_wrap");
    const SettingsKeyResult r = f.view.handle_key(key(Key::Enter));
    REQUIRE(r.change != nullptr);
    CHECK(r.change->key == "word_wrap");
    CHECK(r.value == 0);
}

TEST_CASE("the colors row shows how many colors were changed, and Enter asks for their editor") {
    Fixture f;
    const std::size_t i = index_of("colors");
    CHECK(f.view.row_text(i).ends_with("defaults"));
    REQUIRE(f.settings.set_raw("colors", *Json::parse(R"({"keyword": "bold red"})")));
    CHECK(f.view.row_text(i).ends_with("1 changed"));
    f.select("colors");
    const SettingsKeyResult r = f.view.handle_key(key(Key::Enter));
    REQUIRE(r.edit != nullptr);
    CHECK(r.edit->type == SettingType::colors);
    CHECK(f.view.handle_key(ch(U' ')).change == nullptr);
}

TEST_CASE("a choice row shows its name; Enter, Space and Right step to the next name, Left back") {
    Fixture f;
    const std::size_t i = index_of("terminal_mode");
    CHECK(f.view.row_text(i).ends_with("auto"));
    f.select("terminal_mode");
    SettingsKeyResult r = f.view.handle_key(key(Key::Enter));
    REQUIRE(r.change != nullptr);
    CHECK(r.value == 1);
    r = f.view.handle_key(key(Key::Left));
    REQUIRE(r.change != nullptr);
    CHECK(r.value == 2);  // wraps from the first to the last
    REQUIRE(f.settings.set("terminal_mode", 2));
    r = f.view.handle_key(ch(U' '));
    REQUIRE(r.change != nullptr);
    CHECK(r.value == 0);  // wraps from the last to the first
    r = f.view.handle_key(key(Key::Right));
    REQUIRE(r.change != nullptr);
    CHECK(r.value == 0);
}
