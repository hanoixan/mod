#include <doctest/doctest.h>

#include <span>
#include <string>

#include "platform/terminal.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"

using namespace mod;

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

KeyEvent key(Key k, std::uint8_t mods = 0) { return KeyEvent{k, 0, mods}; }
KeyEvent ch(char32_t c) { return KeyEvent{Key::Char, c, 0}; }

std::string row_text(const Screen& s, int row) {
    std::string out;
    for (int c = 0; c < s.cols(); ++c) {
        const Cell& cell = s.cell(row, c);
        if (cell.width == 0) continue;
        out.append(cell.utf8.data(), cell.len);
    }
    return out;
}

}  // namespace

#include <sys/stat.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "env_var.hpp"
#include "platform/path_text.hpp"
#include "ui/file_dialog.hpp"
#include "ui/theme.hpp"

namespace fs = std::filesystem;

namespace {

fs::path make_tree(const std::string& name) {
    const fs::path d = fs::path(MOD_TEST_SCRATCH) / "file_dialog_test" / name;
    fs::remove_all(d);
    fs::create_directories(d / "docs");
    fs::create_directories(d / "src");
    for (const char* f : {"a.txt", "b.md", "c.txt", ".hidden"}) std::ofstream(d / f) << f;
    return d;
}

// A dialog over a fresh tree, dot-files hidden (as in ../modi/). It opens in the File field;
// most tests start from the list, so the fixture moves there unless `in_list` is false.
struct Fixture {
    fs::path dir;
    FileDialog dlg;
    static constexpr int rows = 6;

    Fixture(const std::string& name, FileDialogMode mode = FileDialogMode::open, std::string initial = {}, bool in_list = true)
        : dir(make_tree(name)), dlg(mode, dir, std::move(initial), false) {
        if (in_list) focus(FocusZone::list);
    }

    FileDialogResult press(KeyEvent k) { return dlg.handle_key(k, rows); }
    FileDialogResult press(Key k, std::uint8_t mods = 0) { return press(key(k, mods)); }
    void type(std::string_view s) {
        for (const char c : s) press(ch(static_cast<char32_t>(c)));
    }
    void focus(FocusZone z) {
        for (int i = 0; i < 12 && dlg.focus() != z; ++i) press(Key::Tab);
        REQUIRE(dlg.focus() == z);
    }
    std::string name_at(int i) { return dlg.entries()[static_cast<std::size_t>(i)].name; }
};

}  // namespace

TEST_CASE("the dialog opens in the File field with nothing selected, listing folders then files") {
    Fixture f("open", FileDialogMode::open, {}, false);
    CHECK(f.dlg.focus() == FocusZone::filename);
    CHECK(f.dlg.selected() == -1);
    REQUIRE(f.dlg.entries().size() == 5);
    CHECK(f.name_at(0) == "docs");
    CHECK(f.name_at(1) == "src");
    CHECK(f.name_at(2) == "a.txt");
    CHECK(f.dlg.current_dir() == f.dir);
}

TEST_CASE("selection movement and the File field") {
    Fixture f("select");
    f.press(Key::Up);
    CHECK(f.dlg.selected() == 0);
    f.press(Key::Down);
    CHECK(f.dlg.selected() == 1);
    CHECK(f.dlg.filename().empty());  // a folder does not fill the field
    f.press(Key::Down);
    CHECK(f.dlg.filename() == "a.txt");
    f.press(Key::Up);
    CHECK(f.dlg.filename() == "a.txt");
    f.press(Key::End);
    CHECK(f.dlg.selected() == 4);
    CHECK(f.dlg.filename() == "c.txt");
    f.press(Key::Home);
    CHECK(f.dlg.selected() == 0);
    f.press(Key::PageDown);
    CHECK(f.dlg.selected() == 4);  // 6 rows, 5 entries: clamped
    f.press(Key::PageUp);
    CHECK(f.dlg.selected() == 0);
}

TEST_CASE("PageDown from no selection, and scrolling keeps the selection visible") {
    Fixture f("page");
    f.press(Key::PageDown);
    CHECK(f.dlg.selected() == 4);
    // With two list rows the window must follow the selection.
    Fixture g("page2");
    g.dlg.handle_key(key(Key::Home), 2);
    g.dlg.handle_key(key(Key::End), 2);
    CHECK(g.dlg.selected() == 4);
    CHECK(g.dlg.scroll() == 3);
    g.dlg.handle_key(key(Key::Home), 2);
    CHECK(g.dlg.scroll() == 0);
}

TEST_CASE("Enter enters a folder, Backspace goes up, Enter on a file chooses it") {
    Fixture f("enter");
    f.press(Key::Down);  // docs
    f.press(Key::Enter);
    CHECK(f.dlg.current_dir() == f.dir / "docs");
    CHECK(f.dlg.selected() == -1);
    CHECK(f.dlg.scroll() == 0);
    CHECK(f.dlg.entries().empty());
    f.press(Key::Backspace);
    CHECK(f.dlg.current_dir() == f.dir);
    f.press(Key::PageDown);
    f.press(Key::Up);
    f.press(Key::Up);  // c.txt -> b.md -> a.txt: index 2
    const auto r = f.press(Key::Enter);
    REQUIRE(r.kind == FileDialogResult::Kind::chosen);
    CHECK(r.path == f.dir / "a.txt");
}

TEST_CASE("Enter with nothing selected submits the File field, if any") {
    Fixture f("field");
    CHECK(f.press(Key::Enter).kind == FileDialogResult::Kind::pending);
    f.focus(FocusZone::filename);
    f.type("new.txt");
    const auto r = f.press(Key::Enter);
    REQUIRE(r.kind == FileDialogResult::Kind::chosen);
    CHECK(r.path == f.dir / "new.txt");

    Fixture g("field2", FileDialogMode::save, "x");
    g.focus(FocusZone::filename);
    g.press(Key::Home);
    for (int i = 0; i < 1; ++i) g.press(Key::Delete);
    CHECK(g.press(Key::Enter).kind == FileDialogResult::Kind::pending);  // empty: nothing

    Fixture h("field3");
    h.focus(FocusZone::filename);
    h.type((h.dir / "elsewhere.txt").string());
    CHECK(h.press(Key::Enter).path == h.dir / "elsewhere.txt");  // an absolute name is taken as is

    Fixture d("field4");
    d.focus(FocusZone::filename);
    d.type("docs");
    CHECK(d.press(Key::Enter).kind == FileDialogResult::Kind::pending);
    CHECK(d.dlg.current_dir() == d.dir / "docs");
}

TEST_CASE("Tab and Shift+Tab walk the twelve zones in order and wrap") {
    Fixture f("zones");
    const FocusZone order[] = {FocusZone::filename, FocusZone::submit, FocusZone::cancel,        FocusZone::up,
                               FocusZone::home,     FocusZone::folder, FocusZone::hidden,        FocusZone::filter,
                               FocusZone::sort_name, FocusZone::sort_size, FocusZone::sort_modified, FocusZone::list};
    for (const FocusZone z : order) {
        f.press(Key::Tab);
        CHECK(f.dlg.focus() == z);
    }
    f.press(Key::BackTab);
    CHECK(f.dlg.focus() == FocusZone::sort_modified);
    f.press(Key::Tab, kShift);
    CHECK(f.dlg.focus() == FocusZone::sort_size);
}

TEST_CASE("Left and Right move between zones, but not from inside the File field's text") {
    Fixture f("lr");
    f.press(Key::Left);
    CHECK(f.dlg.focus() == FocusZone::sort_modified);
    f.press(Key::Right);
    CHECK(f.dlg.focus() == FocusZone::list);
    f.press(Key::Right);
    CHECK(f.dlg.focus() == FocusZone::filename);
    f.type("ab");
    f.press(Key::Left);
    CHECK(f.dlg.focus() == FocusZone::filename);
    f.press(Key::Home);
    f.press(Key::Left);
    CHECK(f.dlg.focus() == FocusZone::list);
    f.press(Key::Right);
    f.press(Key::End);
    f.press(Key::Right);
    CHECK(f.dlg.focus() == FocusZone::submit);
    f.press(Key::Right);
    CHECK(f.dlg.focus() == FocusZone::cancel);
    CHECK(f.press(Key::Enter).kind == FileDialogResult::Kind::canceled);
}

TEST_CASE("Home goes to the home folder, and a typed ~ is it, on every system") {
    Fixture f("home");
    const test::EnvVar home("HOME", f.dir.string());
    const test::EnvVar profile("USERPROFILE", display_path(f.dir));  // Windows' home
    f.press(Key::Down);
    f.press(Key::Enter);  // into docs
    REQUIRE(f.dlg.current_dir() == f.dir / "docs");
    f.focus(FocusZone::home);
    f.press(Key::Enter);
    CHECK(f.dlg.current_dir() == f.dir);

    f.press(Key::Down);
    f.press(Key::Enter);  // into docs again
    f.focus(FocusZone::filename);
    f.type("~/src");
    CHECK(f.press(Key::Enter).kind == FileDialogResult::Kind::pending);
    CHECK(f.dlg.current_dir() == f.dir / "src");

    Fixture g("home2");
    g.press(Key::Down);
    g.press(Key::Enter);  // into docs; home is still f.dir
    g.focus(FocusZone::filename);
    g.type("~/new.txt");
    const auto r = g.press(Key::Enter);
    REQUIRE(r.kind == FileDialogResult::Kind::chosen);
    CHECK(r.path == f.dir / "new.txt");
}

TEST_CASE("the toolbar buttons: Up, hidden files, Submit") {
    Fixture f("buttons");
    f.press(Key::Down);
    f.press(Key::Enter);  // into docs
    f.focus(FocusZone::up);
    f.press(Key::Enter);
    CHECK(f.dlg.current_dir() == f.dir);
    f.focus(FocusZone::hidden);
    CHECK_FALSE(f.dlg.show_hidden());
    f.press(Key::Enter);
    CHECK(f.dlg.show_hidden());
    CHECK(f.dlg.entries().size() == 6);
    f.press(Key::Enter);
    CHECK(f.dlg.entries().size() == 5);
    f.focus(FocusZone::filename);
    f.type("z.txt");
    f.press(Key::Tab);
    CHECK(f.dlg.focus() == FocusZone::submit);
    CHECK(f.press(Key::Enter).path == f.dir / "z.txt");
}

TEST_CASE("the sort headers: a new column sorts ascending, the same column flips, folders stay first") {
    Fixture f("sort");
    f.focus(FocusZone::sort_name);
    f.press(Key::Enter);  // name was ascending: flips
    CHECK(f.name_at(0) == "docs");
    CHECK(f.name_at(2) == "c.txt");
    f.press(Key::Enter);
    CHECK(f.name_at(2) == "a.txt");
    f.focus(FocusZone::sort_size);
    f.press(Key::Enter);
    CHECK(f.name_at(0) == "docs");
    CHECK(f.name_at(1) == "src");
    f.focus(FocusZone::sort_modified);
    f.press(Key::Enter);
    CHECK(f.name_at(0) == "docs");
}

TEST_CASE("Escape cancels") {
    Fixture f("esc");
    CHECK(f.press(Key::Escape).kind == FileDialogResult::Kind::canceled);
}

TEST_CASE("typing in the File field selects the first name starting with it, else containing it, and keeps the text") {
    Fixture f("search", FileDialogMode::open, {}, false);  // docs, src, a.txt, b.md, c.txt
    f.type("B");
    CHECK(f.dlg.filename() == "B");  // the field keeps what was typed
    CHECK(f.name_at(f.dlg.selected()) == "b.md");
    CHECK(f.dlg.match() == "b.md");
    CHECK(f.dlg.completion() == ".md");  // the rest of the name, after the typed text
    f.type(".m");
    CHECK(f.dlg.completion() == "d");
    f.type("d");
    CHECK(f.dlg.completion().empty());  // the whole name typed: nothing to add
    // A name that starts with the text comes before one that only contains it.
    for (int i = 0; i < 4; ++i) f.press(Key::Backspace);
    f.type("s");
    CHECK(f.name_at(f.dlg.selected()) == "src");
    CHECK(f.dlg.completion() == "rc");
    // Only containing it: the whole name is offered.
    f.press(Key::Backspace);
    f.type("xt");
    CHECK(f.name_at(f.dlg.selected()) == "a.txt");
    CHECK(f.dlg.completion() == " → a.txt");
    // Nothing matches: no match, the selection stays.
    f.type("zz");
    CHECK(f.dlg.match().empty());
    CHECK(f.dlg.completion().empty());
    CHECK(f.name_at(f.dlg.selected()) == "a.txt");
    // A path is not looked up.
    for (int i = 0; i < 4; ++i) f.press(Key::Backspace);
    f.type("docs/");
    CHECK(f.dlg.match().empty());
}

TEST_CASE("Right at the field's end takes the completion; with none it moves on") {
    Fixture f("complete", FileDialogMode::open, {}, false);
    f.type("c");
    f.press(Key::Right);
    CHECK(f.dlg.filename() == "c.txt");
    CHECK(f.dlg.focus() == FocusZone::filename);
    CHECK(f.dlg.completion().empty());
    f.press(Key::Right);
    CHECK(f.dlg.focus() == FocusZone::submit);

    Fixture g("complete2", FileDialogMode::open, {}, false);
    g.type("XT");
    g.press(Key::Right);
    CHECK(g.dlg.filename() == "a.txt");  // a match inside the name replaces the text with it

    Fixture h("complete3", FileDialogMode::open, {}, false);
    h.type("cx");
    h.press(Key::Left);
    CHECK(h.dlg.completion().empty());  // the cursor is not at the end
    h.press(Key::Right);
    CHECK(h.dlg.filename() == "cx");
}

TEST_CASE("a typed folder name and Enter goes into it and empties the field") {
    Fixture f("enterdir", FileDialogMode::open, {}, false);
    f.type("doc");
    f.press(Key::Right);
    CHECK(f.press(Key::Enter).kind == FileDialogResult::Kind::pending);
    CHECK(f.dlg.current_dir() == f.dir / "docs");
    CHECK(f.dlg.filename().empty());
}

TEST_CASE("typing in the list goes to the File field; Shift+Up and Shift+Down step through the names containing it") {
    Fixture f("listtype");
    f.type("t");
    CHECK(f.dlg.focus() == FocusZone::filename);
    CHECK(f.dlg.filename() == "t");
    CHECK(f.name_at(f.dlg.selected()) == "a.txt");  // contains t; nothing starts with it
    f.focus(FocusZone::list);
    f.press(Key::Down, kShift);
    CHECK(f.name_at(f.dlg.selected()) == "c.txt");
    CHECK(f.dlg.filename() == "t");  // stepping keeps the text
    f.press(Key::Down, kShift);
    CHECK(f.name_at(f.dlg.selected()) == "a.txt");  // wraps
    f.press(Key::Up, kShift);
    CHECK(f.name_at(f.dlg.selected()) == "c.txt");
}

TEST_CASE("the completion is drawn lighter after the typed text") {
    Fixture f("ghost", FileDialogMode::open, {}, false);
    f.type("b");
    NullTerminal term;
    Screen screen{term};
    screen.resize({14, 60});
    f.dlg.render(screen, Rect{0, 0, 14, 60});
    CHECK(row_text(screen, 12).find("File: b.md") != std::string::npos);
    const int typed = static_cast<int>(std::string(" File: ").size());
    CHECK(screen.cell(12, typed).attr == attr_for(Style::list_selected));
    CHECK((screen.cell(12, typed + 1).attr.flags & kDim) != 0);
    CHECK((screen.cell(12, typed + 1).attr.flags & kBold) == 0);
    CHECK(row_text(screen, 13).find("⌕") == std::string::npos);  // no separate search box
}

TEST_CASE("clicking a part of the path goes to that folder") {
    Fixture f("crumbs");
    f.press(Key::Down);
    f.press(Key::Enter);  // into docs
    REQUIRE(f.dlg.current_dir() == f.dir / "docs");
    NullTerminal term;
    Screen screen{term};
    screen.resize({14, 140});
    f.dlg.render(screen, Rect{0, 0, 14, 140});
    const std::string path = row_text(screen, 1);
    const std::string name = "crumbs";
    const std::size_t at = path.rfind(name);
    REQUIRE(at != std::string::npos);
    int col = 0;  // the column of byte `at`: one per character, as row_text joins one cell per column
    for (std::size_t i = 0; i < at;) {
        const auto c = static_cast<unsigned char>(path[i]);
        i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        ++col;
    }
    CHECK(f.dlg.click(1, col + 1, false, Fixture::rows).kind == FileDialogResult::Kind::pending);
    CHECK(f.dlg.current_dir() == f.dir);
#if !defined(__CYGWIN__)  // there the first part is the drive, D:, not /
    f.dlg.render(screen, Rect{0, 0, 14, 140});
    CHECK(f.dlg.click(1, 1, false, Fixture::rows).kind == FileDialogResult::Kind::pending);  // the first part: the root
    CHECK(f.dlg.current_dir() == f.dir.root_path());
    f.dlg.render(screen, Rect{0, 0, 14, 140});
    f.dlg.click(1, 3, false, Fixture::rows);  // a separator: nothing
    CHECK(f.dlg.current_dir() == f.dir.root_path());
#endif
}

TEST_CASE("new folder: Enter creates it, empty creates nothing, Escape leaves the mode") {
    Fixture f("folder");
    f.focus(FocusZone::folder);
    f.press(Key::Enter);
    CHECK(f.dlg.editing_folder());
    f.type("newdir");
    f.press(Key::Enter);
    CHECK_FALSE(f.dlg.editing_folder());
    CHECK(fs::is_directory(f.dir / "newdir"));
    CHECK(f.dlg.entries().size() == 6);

    f.focus(FocusZone::folder);
    f.press(Key::Enter);
    f.press(Key::Enter);  // empty
    CHECK_FALSE(f.dlg.editing_folder());
    CHECK(f.dlg.entries().size() == 6);

    f.press(Key::Enter);
    f.type("nope");
    CHECK(f.press(Key::Escape).kind == FileDialogResult::Kind::pending);  // the dialog stays open
    CHECK_FALSE(f.dlg.editing_folder());
    CHECK_FALSE(fs::exists(f.dir / "nope"));
}

TEST_CASE("new folder: a failure shows its error and the dialog stays usable") {
    Fixture f("folderfail");
    std::ofstream(f.dir / "afile") << "x";
    f.focus(FocusZone::folder);
    f.press(Key::Enter);
    f.type("afile/sub");
    f.press(Key::Enter);
    CHECK_FALSE(f.dlg.error().empty());
    f.press(Key::Tab);
    f.press(Key::Tab);
    CHECK(f.dlg.focus() == FocusZone::filter);
}

TEST_CASE("the filter: Enter applies it, a blank keeps the old one, Escape and Tab leave without applying") {
    Fixture f("filter");
    f.focus(FocusZone::filter);
    f.press(Key::Enter);
    CHECK(f.dlg.editing_filter());
    for (int i = 0; i < 3; ++i) f.press(Key::Backspace);
    f.type("*.txt");
    f.press(Key::Enter);
    CHECK_FALSE(f.dlg.editing_filter());
    CHECK(f.dlg.filter() == "*.txt");
    CHECK(f.dlg.entries().size() == 4);  // docs, src, a.txt, c.txt

    f.press(Key::Enter);
    for (int i = 0; i < 5; ++i) f.press(Key::Backspace);
    f.press(Key::Enter);
    CHECK(f.dlg.filter() == "*.txt");

    f.press(Key::Enter);
    f.type("x");
    f.press(Key::Escape);
    CHECK_FALSE(f.dlg.editing_filter());
    CHECK(f.dlg.filter() == "*.txt");

    f.press(Key::Enter);
    f.type("x");
    f.press(Key::Tab);
    CHECK_FALSE(f.dlg.editing_filter());
    CHECK(f.dlg.filter() == "*.txt");
    CHECK(f.dlg.focus() == FocusZone::sort_name);
}

TEST_CASE("a vanished directory shows the error row and an empty list; Up still works") {
    Fixture g("unreadable");
    g.press(Key::Down);
    g.press(Key::Enter);  // into docs
    fs::remove_all(g.dir / "docs");
    g.focus(FocusZone::hidden);
    g.press(Key::Enter);  // the refresh reports it
    CHECK_FALSE(g.dlg.error().empty());
    CHECK(g.dlg.entries().empty());
    g.focus(FocusZone::up);
    g.press(Key::Enter);
    CHECK(g.dlg.current_dir() == g.dir);
    CHECK(g.dlg.error().empty());
}

TEST_CASE("paste goes to the focused field only") {
    Fixture f("paste");
    f.dlg.handle_paste("ignored");
    CHECK(f.dlg.filename().empty());
    f.focus(FocusZone::filename);
    f.dlg.handle_paste("pasted.txt\n");
    CHECK(f.dlg.filename() == "pasted.txt");
}

TEST_CASE("render draws the layout") {
    Fixture f("render", FileDialogMode::save, "x.txt");
    NullTerminal term;
    Screen screen{term};
    screen.resize({14, 140});
    f.press(Key::Down);
    f.press(Key::Down);
    f.press(Key::Down);  // a.txt selected
    f.dlg.render(screen, Rect{0, 0, 14, 140});
    CHECK(row_text(screen, 0).rfind("── Save As ─", 0) == 0);
#if defined(__CYGWIN__)
    CHECK(row_text(screen, 1).substr(2, 3) == ": >");  // " D: > a > …", from the drive
#else
    CHECK(row_text(screen, 1).substr(0, 4) == " / >");
#endif
    CHECK(row_text(screen, 1).find("> file_dialog_test > render") != std::string::npos);
    const std::string toolbar = row_text(screen, 2);
    CHECK(toolbar.find("[↑Up] [⌂Home] [+Folder]") != std::string::npos);
    CHECK(toolbar.find("○Hidden [*.*]") != std::string::npos);
    const std::string header = row_text(screen, 3);
    CHECK(header.find("Name ▲") != std::string::npos);
    CHECK(header.find("Size") != std::string::npos);
    CHECK(header.find("Modified") != std::string::npos);
    CHECK(row_text(screen, 4).rfind("───", 0) == 0);
    CHECK(row_text(screen, 5).find("docs") != std::string::npos);
    CHECK(row_text(screen, 5).find("📁") != std::string::npos);
    CHECK(row_text(screen, 7).find("a.txt") != std::string::npos);
    CHECK(row_text(screen, 7).find("📄") != std::string::npos);
    CHECK(screen.cell(7, 5).attr == attr_for(Style::list_selected));  // the selected row
    CHECK(row_text(screen, 12).find("File: a.txt") != std::string::npos);  // selecting a file fills the field
    const std::string actions = row_text(screen, 13);
    CHECK(actions.find("[Save] [Cancel]") != std::string::npos);
    CHECK(actions.find("3 of 5") != std::string::npos);
}

TEST_CASE("the error replaces the last list row; a one-row body and narrow widths do not break") {
    Fixture g("errorrow");
    g.press(Key::Down);
    g.press(Key::Enter);
    fs::remove_all(g.dir / "docs");
    g.focus(FocusZone::hidden);
    g.press(Key::Enter);
    NullTerminal term;
    Screen screen{term};
    screen.resize({12, 40});
    g.dlg.render(screen, Rect{0, 0, 12, 40});
    CHECK(screen.cell(8, 1).attr == attr_for(Style::error));  // 4 list rows, rows 5-8: the last holds the error
    Screen tiny{term};
    tiny.resize({1, 8});
    g.dlg.render(tiny, Rect{0, 0, 1, 8});
    CHECK(FileDialog::list_rows_for(1) == 1);
    CHECK(FileDialog::list_rows_for(20) == 12);
}

TEST_CASE("dot-files are shown by default; the Hidden toggle hides them") {
    const fs::path d = make_tree("hidden_default");
    FileDialog dlg(FileDialogMode::open, d, {});
    CHECK(dlg.show_hidden());
    CHECK(dlg.entries().size() == 6);  // docs, src, .hidden, a.txt, b.md, c.txt
}
