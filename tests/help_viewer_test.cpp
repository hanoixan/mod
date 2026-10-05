#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "platform/terminal.hpp"
#include "ui/help_viewer.hpp"
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

std::string screen_row(const Screen& s, int row) {
    std::string out;
    for (int c = 0; c < s.cols(); ++c) {
        const Cell& cell = s.cell(row, c);
        out.append(cell.utf8.data(), cell.len);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void write_file(const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; }

// A small manual: an index with several links, two pages with headings.
struct Fixture {
    fs::path root = fs::path(MOD_TEST_SCRATCH) / "help_viewer_test";
    NullTerminal term;
    Screen screen{term};
    Rect area{0, 0, 6, 60};
    HelpViewer viewer;
    Fixture() {
        fs::remove_all(root);
        fs::create_directories(root);
        std::string index = "# Index\n\nIntro [Alpha](alpha.md) and [Beta](beta.md#second).\n\n[Web](https://example.com) and [Missing](missing.md).\n\n";
        for (int i = 0; i < 20; ++i) index += "Filler line " + std::to_string(i) + ".\n\n";
        index += "[Last](alpha.md)\n";
        write_file(root / "index.md", index);
        std::string alpha = "# Alpha\n\nBack to [index](index.md). Jump [down](#end).\n\n";
        for (int i = 0; i < 20; ++i) alpha += "Alpha line " + std::to_string(i) + ".\n\n";
        alpha += "## End\n\nbottom\n";
        write_file(root / "alpha.md", alpha);
        write_file(root / "beta.md", "# Beta\n\nx\n\n## Second\n\ny\n");
        screen.resize({8, 60});
        REQUIRE(viewer.open(root));
        draw();
    }
    void draw() { viewer.render(screen, area); }
    HelpKeyResult press(KeyEvent k) {
        const HelpKeyResult r = viewer.handle_key(k);
        draw();
        return r;
    }
    std::string row(int r) const { return screen_row(screen, r); }
};

}  // namespace

TEST_CASE("the viewer opens on the index, rendered") {
    Fixture f;
    CHECK(f.viewer.is_open());
    CHECK(f.viewer.page() == "index.md");
    CHECK(f.row(0) == "Index");
    CHECK(f.row(3) == "Intro Alpha and Beta.");
    CHECK_FALSE(f.viewer.selected_target().has_value());
}

TEST_CASE("Down and Up move between the links on screen, then scroll") {
    Fixture f;
    f.press(key(Key::Down));
    CHECK(f.viewer.selected_target() == "alpha.md");
    f.press(key(Key::Down));
    CHECK(f.viewer.selected_target() == "beta.md#second");
    f.press(key(Key::Down));
    CHECK(f.viewer.selected_target() == "https://example.com");
    f.press(key(Key::Down));
    CHECK(f.viewer.selected_target() == "missing.md");
    CHECK(f.viewer.top() == 0);
    f.press(key(Key::Down));  // no further link on screen: scroll a line
    CHECK(f.viewer.top() == 1);
    CHECK(f.viewer.selected_target() == "missing.md");
    f.press(key(Key::Up));
    CHECK(f.viewer.selected_target() == "https://example.com");
    f.press(key(Key::Up));
    f.press(key(Key::Up));
    CHECK(f.viewer.selected_target() == "alpha.md");
    f.press(key(Key::Up));  // no link above on screen: scroll up
    CHECK(f.viewer.top() == 0);
}

TEST_CASE("the highlighted link is drawn in the selected look, other links in the link look") {
    Fixture f;
    f.press(key(Key::Down));
    const int alpha = static_cast<int>(f.row(3).find("Alpha"));
    const int beta = static_cast<int>(f.row(3).find("Beta"));
    CHECK(f.screen.cell(3, alpha).attr == attr_for(Style::menu_selected));
    CHECK(f.screen.cell(3, beta).attr == attr_for(Style::md_link_text));
}

TEST_CASE("Enter follows a page link and Left comes back to the place and the link") {
    Fixture f;
    f.press(key(Key::Down));
    const HelpKeyResult r = f.press(key(Key::Enter));
    CHECK(r.message.empty());
    CHECK(f.viewer.page() == "alpha.md");
    CHECK(f.row(0) == "Alpha");
    CHECK_FALSE(f.viewer.selected_target().has_value());
    f.press(key(Key::Left));
    CHECK(f.viewer.page() == "index.md");
    CHECK(f.viewer.selected_target() == "alpha.md");
}

TEST_CASE("Right follows page#anchor and #anchor links to their heading") {
    Fixture f;
    f.press(key(Key::Down));
    f.press(key(Key::Down));
    f.press(key(Key::Right));
    CHECK(f.viewer.page() == "beta.md");
    CHECK(f.row(0) == "Second");
    f.press(key(Key::Left));
    f.press(key(Key::Up));  // back on Beta: up to Alpha
    f.press(key(Key::Enter));
    CHECK(f.viewer.page() == "alpha.md");
    f.press(key(Key::Down));
    f.press(key(Key::Down));
    CHECK(f.viewer.selected_target() == "#end");
    f.press(key(Key::Enter));
    CHECK(f.viewer.page() == "alpha.md");
    CHECK(f.row(0) == "End");
}

TEST_CASE("a web link and a missing page are not followed but reported") {
    Fixture f;
    for (int i = 0; i < 3; ++i) f.press(key(Key::Down));
    HelpKeyResult r = f.press(key(Key::Enter));
    CHECK(r.message.find("https://example.com") != std::string::npos);
    CHECK(f.viewer.page() == "index.md");
    f.press(key(Key::Down));
    r = f.press(key(Key::Enter));
    CHECK(r.message.starts_with("cannot open missing.md"));
    CHECK(f.viewer.page() == "index.md");
}

TEST_CASE("PageDown, PageUp, Home and End scroll; a hidden highlight gives way") {
    Fixture f;
    f.press(key(Key::Down));
    f.press(key(Key::PageDown));
    CHECK(f.viewer.top() == 6);
    CHECK(f.viewer.selected_target() != "alpha.md");  // scrolled out of sight
    f.press(key(Key::PageUp));
    CHECK(f.viewer.top() == 0);
    f.press(key(Key::End));
    CHECK(f.viewer.top() > 30);
    CHECK(f.row(5) == "Last");
    CHECK(f.viewer.selected_target() == "alpha.md");  // the first link on the new screen
    f.press(key(Key::Home));
    CHECK(f.viewer.top() == 0);
}

TEST_CASE("search keys, Esc, reopening where it was left, and show for a match") {
    Fixture f;
    CHECK(f.press(KeyEvent{Key::Char, U'/', 0}).search);
    CHECK(f.press(KeyEvent{Key::CtrlLetter, U'f', kCtrl}).search);
    f.press(key(Key::Down));
    f.press(key(Key::Enter));
    REQUIRE(f.viewer.page() == "alpha.md");
    CHECK(f.press(key(Key::Escape)).closed);
    CHECK_FALSE(f.viewer.is_open());
    REQUIRE(f.viewer.open(f.root));
    f.draw();
    CHECK(f.viewer.page() == "alpha.md");
    REQUIRE(f.viewer.show("beta.md", 5));
    f.draw();
    CHECK(f.viewer.page() == "beta.md");
    CHECK(f.row(0) == "Second");
    f.press(key(Key::Left));
    CHECK(f.viewer.page() == "alpha.md");
}

TEST_CASE("keys the viewer does not use are left to App; a new width re-renders") {
    Fixture f;
    f.viewer.handle_key(KeyEvent{Key::CtrlLetter, U's', kCtrl});
    CHECK_FALSE(f.viewer.used());
    f.viewer.handle_key(key(Key::Down));
    CHECK(f.viewer.used());
    f.screen.resize({8, 20});
    f.area = Rect{0, 0, 6, 20};
    f.draw();
    CHECK(f.row(3) == "Intro Alpha and");
    CHECK(f.row(4) == "Beta.");
}
