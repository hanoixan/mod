// The real App, its event loop on a scripted terminal: keys in, the screen out.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <chrono>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "app/app.hpp"
#include "platform/file_map.hpp"
#include "scripted_terminal.hpp"

using namespace mod;
using namespace mod::testing;
namespace fs = std::filesystem;

namespace {

struct Run {
    std::vector<ScriptStep> steps;
    std::string last_screen;
    std::vector<std::string> titles;
};

struct File {
    std::string name;
    std::string text;
};

struct Setup {
    bool read_only = false;
    std::string term = "xterm-256color";
    std::string settings_json;  // settings.json's text, if any
};

Setup read_only_setup() {
    Setup s;
    s.read_only = true;
    return s;
}

Setup term_setup(std::string term) {
    Setup s;
    s.term = std::move(term);
    return s;
}

Setup settings_setup(std::string json) {
    Setup s;
    s.settings_json = std::move(json);
    return s;
}

// Runs App on `files` (each opened from the command line, the first shown) through
// `steps`; returns the steps as they ran, the screen at the end and every title set.
Run run_app(const std::string& name, const std::vector<File>& files, std::vector<ScriptStep> steps, const Setup& setup = {}) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "app_test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir / "config");
    ::setenv("XDG_CONFIG_HOME", (dir / "config").c_str(), 1);
    ::setenv("TERM", setup.term.c_str(), 1);
    if (!setup.settings_json.empty()) {
        fs::create_directories(dir / "config" / "mod");
        std::ofstream(dir / "config" / "mod" / "settings.json") << setup.settings_json;
    }
    CliOptions options;
    options.view = setup.read_only;
    for (const File& f : files) {
        fs::create_directories((dir / f.name).parent_path());
        std::ofstream(dir / f.name, std::ios::binary) << f.text;
        if (f.name.find('/') == std::string::npos) options.paths.push_back(dir / f.name);
    }
    auto owned = std::make_unique<ScriptedTerminal>(12, 80, std::move(steps));
    ScriptedTerminal* term = owned.get();
    Run run;
    // The working folder is the test's: the folder tree's root.
    const fs::path cwd = fs::current_path();
    fs::current_path(dir);
    struct Back {
        fs::path to;
        ~Back() { fs::current_path(to); }
    } back{cwd};
    {
        App app(options, std::move(owned));
        CHECK(app.run() == 0);
        run.steps = term->steps();
        for (int r = 0; r < term->screen().rows(); ++r) run.last_screen += term->screen().row(r) + "\n";
        run.titles = term->screen().titles();
    }
    return run;
}

Run run_app(const std::string& name, const std::string& text, std::vector<ScriptStep> steps) {
    return run_app(name, {File{"file.txt", text}}, std::move(steps));
}

void check_steps(const Run& run) {
    CAPTURE(run.last_screen);
    for (const ScriptStep& s : run.steps) {
        CAPTURE(s.name);
        CHECK(s.done);
    }
}

}  // namespace

namespace {

// The rows of the status lines naming `name`, top to bottom, with their first character.
std::vector<std::pair<int, char>> status_rows(const VtScreen& s, std::string_view name) {
    std::vector<std::pair<int, char>> out;
    for (int r = 0; r < s.rows(); ++r) {
        const std::string row = s.row(r);
        if (row.size() > name.size() && row.substr(1, name.size()) == name) out.emplace_back(r, row[0]);
    }
    return out;
}

}  // namespace

TEST_CASE("Find opens with the last search, not with what another prompt was given") {
    std::string find_bar;
    auto find_open = [&find_bar](const VtScreen& s) {
        for (int r = 0; r < s.rows(); ++r) {
            if (s.row(r).starts_with(" Find: ")) {
                find_bar = s.row(r);
                return true;
            }
        }
        return false;
    };
    const auto run = run_app("find-after-goto", "one\ntwo\nthree\nfour\n",
                             {{"search", "\x06two\r", [](const VtScreen& s) { return s.status().find(" 2:") != std::string::npos; }},
                              {"close the find bar", "\x1b", nullptr},
                              {"go to line 4", "\x07" "4\r", [](const VtScreen& s) { return s.status().find(" 4:1") != std::string::npos; }},
                              {"Find again", "\x06", find_open},
                              {"close it", "\x1b", nullptr},
                              {"quit", "\x11", nullptr}});
    CAPTURE(run.last_screen);
    for (const ScriptStep& s : run.steps) {
        CAPTURE(s.name);
        CAPTURE(s.timed_out);
        CAPTURE(s.took.count());
        CHECK(s.done);
    }
    CHECK(find_bar.starts_with(" Find: two "));
}


TEST_CASE("the focused view's status line starts with '>', with one view too, and the mark follows the focus") {
    auto marks = [](char top, char bottom) {
        return [top, bottom](const VtScreen& s) {
            const auto rows = status_rows(s, "file.txt");
            return rows.size() == 2 && rows[0].second == top && rows[1].second == bottom;
        };
    };
    auto one_view = [](const VtScreen& s) {
        const auto rows = status_rows(s, "file.txt");
        return rows.size() == 1 && rows[0].second == '>';
    };
    const auto run = run_app("split-marks", "one\ntwo\n",
                             {{"one view: marked too", "", one_view},
                              {"View > Split: the upper keeps the focus", "\x1bxvp", marks('>', ' ')},
                              {"Esc, Shift+Down: the focus moves down", "\x1bx\x1b[1;2B\x1b", marks(' ', '>')},
                              {"Esc, PageUp: not any more", "\x1bx\x1b[5~\x1b", marks(' ', '>')},
                              {"Esc, Shift+Up: back up", "\x1bx\x1b[1;2A\x1b", marks('>', ' ')},
                              {"and down again", "\x1bx\x1b[1;2B\x1b", marks(' ', '>')},
                              {"View > Unsplit: marked", "\x1bxvu", one_view},
                              {"quit", "\x11", nullptr}});
    CAPTURE(run.last_screen);
    for (const ScriptStep& s : run.steps) {
        CAPTURE(s.name);
        CHECK(s.done);
    }
}

TEST_CASE("the terminal's title names the focused view's document, with '*' while it has unsaved changes") {
    // A step's condition sees the screen, which records the titles as they are set.
    auto title = [](std::string want) {
        return [want](const VtScreen& s) { return !s.titles().empty() && s.titles().back() == want; };
    };
    const auto run = run_app("title", {File{"a.txt", "alpha\n"}, File{"b.txt", "beta\n"}},
                             {{"at start", "", title("mod:a.txt")},
                              {"an edit", "x", title("mod:a.txt *")},
                              {"saved", "\x13", title("mod:a.txt")},
                              {"View > Split", "\x1bxvp", [](const VtScreen& s) { return status_rows(s, "a.txt").size() == 2; }},
                              {"the focus moves down", "\x1bx\x1b[1;2B\x1b", [](const VtScreen& s) {
                                   const auto rows = status_rows(s, "a.txt");
                                   return rows.size() == 2 && rows[1].second == '>';
                               }},
                              {"Documents > b.txt in the lower split", "\x1bxd2", title("mod:b.txt")},
                              {"the focus back up", "\x1bx\x1b[1;2A\x1b", title("mod:a.txt")},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
    // Sent only when it changes: never the same title twice in a row.
    for (std::size_t i = 1; i < run.titles.size(); ++i) {
        CAPTURE(i);
        CHECK(run.titles[i] != run.titles[i - 1]);
    }
}

TEST_CASE("following a link in read-only mode leaves the title on the view's own document") {
    const auto run = run_app("title-link", {File{"index.md", "See [the notes](notes.md).\n"}, File{"notes.md", "# Notes\n"}},
                             {{"at start", "", [](const VtScreen& s) { return !s.titles().empty() && s.titles().back() == "mod:index.md"; }},
                              {"Tab to the link, Enter", "\t\r", [](const VtScreen& s) { return s.status().find("notes.md") != std::string::npos; }},
                              {"quit", "\x11", nullptr}},
                             read_only_setup());
    check_steps(run);
    REQUIRE_FALSE(run.titles.empty());
    CHECK(run.titles.back() == "mod:index.md");
}

TEST_CASE("a VT100 is never sent a title") {
    const auto run = run_app("title-vt100", {File{"a.txt", "alpha\n"}}, {{"an edit", "x", nullptr}}, term_setup("vt100"));
    CHECK(run.titles.empty());
}

namespace {

bool row_starts(const VtScreen& s, int r, std::string_view text) { return s.row(r).starts_with(text); }

// Any row showing a document's position, as a status line does.
bool shows_position(const VtScreen& s) {
    static const std::regex re(R"(\d+:\d+ / \d+)");
    for (int r = 0; r < s.rows(); ++r) {
        if (std::regex_search(s.row(r), re)) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("one view: a prompt takes the bottom line and pushes the status line up") {
    const auto run = run_app("bottom-one", "one\ntwo\n",
                             {{"the status line is the bottom line", "", [](const VtScreen& s) { return row_starts(s, 11, ">file.txt"); }},
                              {"Ctrl+F", "\x06", [](const VtScreen& s) { return row_starts(s, 11, " Find:") && row_starts(s, 10, ">file.txt"); }},
                              {"Esc: back", "\x1b", [](const VtScreen& s) { return row_starts(s, 11, ">file.txt"); }},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}

TEST_CASE("two splits: a prompt takes the bottom line from the lower split, the menu bar the top line from the upper; the other split's text dims") {
    const auto run = run_app("bottom-two", "one\ntwo\n",
                             {{"View > Split", "\x1bxvp", [](const VtScreen& s) { return row_starts(s, 5, ">file.txt") && row_starts(s, 11, " file.txt"); }},
                              {"nothing dims yet", "", [](const VtScreen& s) { return !s.dim(6, 4); }},
                              {"Ctrl+G in the upper split", "\x07", [](const VtScreen& s) {
                                   return row_starts(s, 11, " Go to line:") && row_starts(s, 10, " file.txt") && row_starts(s, 5, ">file.txt") &&
                                          s.dim(6, 4) && !s.dim(0, 4);
                               }},
                              {"Esc: the lower split takes its row back", "\x1b", [](const VtScreen& s) { return row_starts(s, 11, " file.txt") && !s.dim(6, 4); }},
                              {"the menu bar is the top line, pushing the upper split down, and dims the other split", "\x1bx", [](const VtScreen& s) {
                                   return s.row(0).find("File") != std::string::npos && s.row(0).find("Edit") != std::string::npos &&
                                          row_starts(s, 1, "  1 one") && row_starts(s, 5, ">file.txt") && row_starts(s, 11, " file.txt") && s.dim(6, 4);
                               }},
                              {"Esc", "\x1b", [](const VtScreen& s) { return row_starts(s, 11, " file.txt"); }},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}

TEST_CASE("a question takes the bottom lines, the status line above it") {
    const auto run = run_app("bottom-question", "one\n",
                             {{"an edit", "x", [](const VtScreen& s) { return s.contains("xone"); }},
                              {"Ctrl+Q asks", "\x11", [](const VtScreen& s) {
                                   // the question's rule, its text and its buttons, the status line above them
                                   return s.row(11).find("Discard") != std::string::npos && s.row(10).find("Save changes") != std::string::npos &&
                                          s.row(9).starts_with("─") && row_starts(s, 8, ">file.txt");
                               }},
                              {"Discard", "\x1b[C\r", nullptr}});
    check_steps(run);
}

TEST_CASE("a full-screen UI has its own bottom line and no document status line") {
    const auto run = run_app("bottom-dialog", "one\n",
                             {{"View > Split", "\x1bxvp", [](const VtScreen& s) { return row_starts(s, 5, ">file.txt"); }},
                              {"File > Open", "\x1bxfo", [](const VtScreen& s) {
                                   return s.row(11).find("Tab: next part") != std::string::npos && s.row(11).find("file.txt") == std::string::npos && !shows_position(s);
                               }},
                              {"Esc", "\x1b", [](const VtScreen& s) { return row_starts(s, 5, ">file.txt"); }},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}

namespace {

// A step that waits `ms` from when it starts, sending nothing.
ScriptStep pause(std::string name, int ms) {
    auto started = std::make_shared<std::optional<std::chrono::steady_clock::time_point>>();
    return {std::move(name), "", [started, ms](const VtScreen&) {
                const auto now = std::chrono::steady_clock::now();
                if (!*started) *started = now;
                return now - **started >= std::chrono::milliseconds(ms);
            }};
}

}  // namespace

namespace {

bool menu_shown(const VtScreen& s) { return s.row(0).find("File") != std::string::npos && s.row(0).find("Edit") != std::string::npos; }

// A key pressed as quickly as the editor takes it: a lone Esc waits only until its effect is
// on the screen (the decoder needs a moment to tell it from Alt), never a fixed time, so the
// presses come well within App's 250 ms however slow the machine's timers are.
ScriptStep quick_key(const char* name, const char* bytes, std::function<bool(const VtScreen&)> until = nullptr) {
    ScriptStep step{name, bytes, std::move(until)};
    step.esc_settle = std::chrono::milliseconds(0);
    return step;
}

}  // namespace

TEST_CASE("three Escapes quit only when each comes within 250 ms of the one before") {
    // About 400 ms apart (the harness's Esc settle): three within two seconds, but each too
    // long after the last, so the editor is still there afterwards.
    const auto slow = run_app("escapes-slow", "one\n",
                              {{"Esc", "\x1b", nullptr},
                               {"Esc", "\x1b", nullptr},
                               {"Esc", "\x1b", nullptr},
                               pause("wait", 400),
                               {"still running", "", [](const VtScreen& s) { return s.contains("one"); }}});
    check_steps(slow);
    // Quick: the menu opens, closes, and the third Esc quits.
    const auto quick = run_app("escapes-quick", "one\n",
                               {quick_key("Esc", "\x1b", menu_shown),
                                quick_key("Esc", "\x1b", [](const VtScreen& s) { return !menu_shown(s); }),
                                quick_key("Esc", "\x1b"),
                                pause("wait", 300),
                                {"never reached: it quit", "", [](const VtScreen&) { return true; }}});
    REQUIRE(quick.steps.size() == 5);
    CHECK(quick.steps[1].done);        // the second Esc closed the menu
    CHECK_FALSE(quick.steps[3].done);  // and the third quit before the pause after it ended
}

TEST_CASE("the menu keys quit like Esc: F10, Alt+X, or any mix, three times quickly") {
    // F10 and Alt+X are whole sequences, so they need no settle; a lone Esc waits for the menu.
    const char* const f10 = "\x1b[21~";
    const char* const alt_x = "\x1bx";
    const std::vector<std::vector<ScriptStep>> runs = {
        {quick_key("F10", f10), quick_key("F10", f10), quick_key("F10", f10)},
        {quick_key("Alt+X", alt_x), quick_key("Alt+X", alt_x), quick_key("Alt+X", alt_x)},
        {quick_key("Esc", "\x1b", menu_shown), quick_key("F10", f10), quick_key("Alt+X", alt_x)},
    };
    int n = 0;
    for (std::vector<ScriptStep> steps : runs) {
        CAPTURE(n);
        steps.push_back(pause("wait", 300));
        steps.push_back({"never reached: it quit", "", [](const VtScreen&) { return true; }});
        const auto r = run_app("menu-keys-quit-" + std::to_string(n), "one\n", steps);
        ++n;
        REQUIRE(r.steps.size() == 5);
        CHECK(r.steps[1].done);        // the second press was read
        CHECK_FALSE(r.steps[3].done);  // and the third quit before the pause after it ended
    }
}

TEST_CASE("another key between the menu keys starts the count again") {
    auto quick = [](const char* name, const char* bytes) {
        ScriptStep step{name, bytes, nullptr};
        step.esc_settle = std::chrono::milliseconds(100);
        return step;
    };
    // Alt+X opens the menu, Down moves in it, Esc closes it, Alt+X again: two in a row at most.
    const auto r = run_app("menu-keys-in-a-row", "one\n",
                           {quick("Alt+X", "\x1bx"), {"Down", "\x1b[B", nullptr}, quick("Esc", "\x1b"), quick("Alt+X", "\x1bx"),
                            pause("wait", 300),
                            {"still running", "", [](const VtScreen& s) { return s.contains("one"); }}});
    check_steps(r);
}

TEST_CASE("Ctrl+Down and Ctrl+Up scroll the view past the cursor; typing brings it back") {
    std::string text;
    for (int i = 0; i < 40; ++i) text += "L" + std::to_string(i) + "\n";
    auto top_shows = [](std::string want) {
        return [want](const VtScreen& s) {
            const std::string row = s.row(0);
            return row.find(" " + want + " ") != std::string::npos || row.ends_with(" " + want);
        };
    };
    const char* const down = "\x1b[1;5B";
    const char* const up = "\x1b[1;5A";
    const auto run = run_app("ctrl-scroll", text,
                             {{"Ctrl+Down three times: L3 at the top, the cursor (on L0) above the view", std::string(down) + down + down, top_shows("L3")},
                              {"Ctrl+Up once", up, top_shows("L2")},
                              {"typing goes in at the cursor, and the view comes back to it", "x", top_shows("xL0")},
                              {"quit without saving", "\x11", [](const VtScreen& s) { return s.contains("Discard"); }},
                              {"Discard", "\x1b[C\r", nullptr}});
    check_steps(run);
}

namespace {

bool row_has(const VtScreen& s, int r, std::string_view text) { return s.row(r).find(text) != std::string::npos; }

}  // namespace

TEST_CASE("folder tree: Shift+Left in escape mode shows it and gives it the keys; Enter opens a file in the view") {
    // The working folder holds config/ (the test's settings), sub/, file.txt and other.txt.
    const auto run = run_app("tree-open", {File{"file.txt", "one\n"}, File{"other.txt", "other text\n"}, File{"sub/inner.txt", "inner\n"}},
                             {{"no tree at first", "", [](const VtScreen& s) { return row_starts(s, 0, "  1 one"); }},
                              {"Esc, Shift+Left: the tree, with the keys", "\x1bx\x1b[1;2D", [](const VtScreen& s) {
                                   return row_starts(s, 0, "▾ tree-open") && row_has(s, 11, "Enter: open") && !row_has(s, 0, "File") &&
                                          row_has(s, 11, " file.txt") && !row_has(s, 11, ">file.txt");
                               }},
                              {"down to other.txt", "\x1b[B\x1b[B\x1b[B\x1b[B", [](const VtScreen& s) { return row_has(s, 4, "other.txt"); }},
                              {"Enter: open in the view, the keys back there (the unpinned tree gone)", "\r", [](const VtScreen& s) {
                                   return row_starts(s, 0, "  1 other text") && row_has(s, 11, ">other.txt");
                               }},
                              {"typing goes to the view", "Z", [](const VtScreen& s) { return s.contains("Zother text"); }},
                              {"quit", "\x11\x1b[C\r", nullptr}});
    check_steps(run);
}

TEST_CASE("folder tree: Space previews a file as the only view, Esc closes it; Esc leaves the tree, Shift+Right goes back to escape mode") {
    const auto run = run_app("tree-preview", {File{"file.txt", "one\n"}, File{"notes.md", "# Notes\n\nSome **bold** text.\n"}},
                             {{"Esc, Shift+Left", "\x1bx\x1b[1;2D", [](const VtScreen& s) { return row_has(s, 11, "Enter: open"); }},
                              {"down to notes.md", "\x1b[B\x1b[B\x1b[B", [](const VtScreen& s) { return row_has(s, 3, "notes.md"); }},
                              {"Space: laid out for reading, alone", " ", [](const VtScreen& s) {
                                   return s.contains("Some bold text.") && row_has(s, 11, "Preview: notes.md") && !s.contains("  1 one");
                               }},
                              {"Esc: the preview goes, the tree keeps the keys", "\x1b", [](const VtScreen& s) {
                                   return s.contains("  1 one") && row_has(s, 11, "Enter: open");
                               }},
                              {"Shift+Right: back to the view in escape mode", "\x1b[1;2C", [](const VtScreen& s) {
                                   return row_has(s, 0, "File") && row_has(s, 0, "Edit") && row_has(s, 11, ">file.txt");
                               }},
                              {"Esc closes the bar", "\x1b", [](const VtScreen& s) { return !row_has(s, 0, "Edit"); }},
                              {"Esc, Shift+Left, Esc: back to editing", "\x1bx\x1b[1;2D", [](const VtScreen& s) { return row_has(s, 11, "Enter: open"); }},
                              {"Esc", "\x1b", [](const VtScreen& s) { return row_has(s, 11, ">file.txt") && !row_has(s, 0, "Edit"); }},
                              {"typing goes to the view", "Q", [](const VtScreen& s) { return s.contains("Qone"); }},
                              {"quit", "\x11\x1b[C\r", nullptr}});
    check_steps(run);
}

TEST_CASE("folder tree: View > Pin Folder Tree keeps it shown or hides it; the pin_folder_tree setting pins it at start") {
    const auto toggled = run_app("tree-toggle", {File{"file.txt", "one\n"}},
                                 {{"View > Pin Folder Tree", "\x1bxvf", [](const VtScreen& s) { return row_starts(s, 0, "▾ tree-toggle"); }},
                                  {"the keys stay in the view", "W", [](const VtScreen& s) { return s.contains("Wone"); }},
                                  {"View > Pin Folder Tree again", "\x1bxvf", [](const VtScreen& s) { return row_starts(s, 0, "  1 Wone"); }},
                                  {"quit", "\x11\x1b[C\r", nullptr}});
    check_steps(toggled);
    const auto at_start = run_app("tree-setting", {File{"file.txt", "one\n"}},
                                  {{"shown at start", "", [](const VtScreen& s) { return row_starts(s, 0, "▾ tree-setting") && s.contains("  1 one"); }},
                                   {"quit", "\x11", nullptr}},
                                  settings_setup(R"({"pin_folder_tree":true})"));
    check_steps(at_start);
}

TEST_CASE("folder tree: a key it does not use runs its command, leaving the tree (Ctrl+Q quits)") {
    const auto run = run_app("tree-command", {File{"file.txt", "one\n"}},
                             {{"Esc, Shift+Left", "\x1bx\x1b[1;2D", [](const VtScreen& s) { return row_has(s, 11, "Enter: open"); }},
                              {"Ctrl+Q", "\x11", nullptr},
                              pause("wait", 400),
                              {"never reached: it quit", "", [](const VtScreen&) { return true; }}});
    REQUIRE(run.steps.size() == 4);
    CHECK(run.steps[1].done);
    CHECK_FALSE(run.steps[3].done);
}

TEST_CASE("folder tree: unpinned, it goes whenever the keys leave it; pinned, it stays") {
    auto tree_shown = [](const VtScreen& s) { return s.contains("▾ tree-ephemeral") || s.contains("▾ tree-pinned"); };
    auto visit = [&](std::string name) {
        return ScriptStep{std::move(name), "\x1bx\x1b[1;2D", [](const VtScreen& s) { return row_has(s, 11, "Enter: open"); }};
    };
    const auto unpinned = run_app("tree-ephemeral", {File{"file.txt", "one\n"}, File{"other.txt", "other\n"}},
                                  {visit("Esc, Shift+Left: the tree, for now"),
                                   {"Shift+Right: gone, back in escape mode", "\x1b[1;2C", [&](const VtScreen& s) { return !tree_shown(s) && row_has(s, 0, "Edit"); }},
                                   {"Esc closes the bar", "\x1b", [&](const VtScreen& s) { return !row_has(s, 0, "Edit"); }},
                                   visit("again"),
                                   {"Esc: gone, back to editing", "\x1b", [&](const VtScreen& s) { return !tree_shown(s) && row_starts(s, 0, "  1 one"); }},
                                   visit("and again"),
                                   {"Enter on other.txt: opened, and the tree gone", "\x1b[B\x1b[B\x1b[B\r", [&](const VtScreen& s) {
                                        return !tree_shown(s) && row_starts(s, 0, "  1 other");
                                    }},
                                   {"quit", "\x11", nullptr}});
    check_steps(unpinned);
    const auto pinned = run_app("tree-pinned", {File{"file.txt", "one\n"}},
                                {visit("Esc, Shift+Left"),
                                 {"Shift+Right: still there", "\x1b[1;2C", [&](const VtScreen& s) { return tree_shown(s) && row_has(s, 0, "Edit"); }},
                                 {"Esc", "\x1b", [&](const VtScreen& s) { return tree_shown(s) && !row_has(s, 0, "Edit"); }},
                                 {"quit", "\x11", nullptr}},
                                settings_setup(R"({"pin_folder_tree":true})"));
    check_steps(pinned);
}

namespace {

// A mouse report for a 0-based screen cell, as a terminal sends it in SGR form.
std::string press(int row, int col, int mods = 0) { return std::format("\x1b[<{};{};{}M", mods, col + 1, row + 1); }
std::string release(int row, int col) { return std::format("\x1b[<0;{};{}m", col + 1, row + 1); }
std::string drag(int row, int col) { return std::format("\x1b[<32;{};{}M", col + 1, row + 1); }
std::string wheel(int row, int col, bool down) { return std::format("\x1b[<{};{};{}M", down ? 65 : 64, col + 1, row + 1); }
std::string click(int row, int col, int mods = 0) { return press(row, col, mods) + release(row, col); }

auto status_has(std::string want) {
    return [want](const VtScreen& s) { return s.status().find(want) != std::string::npos; };
}
auto row_is(int r, std::string want) {
    return [r, want](const VtScreen& s) { return s.row(r).starts_with(want); };
}

// The text column of the first character: right of the line-number gutter.
constexpr int kText = 4;

}  // namespace

TEST_CASE("a click puts the cursor where it lands, the gutter and past a line's end included") {
    const auto run = run_app("mouse-click", "alpha\nbravo\ncharlie\n",
                             {{"start", "", status_has("1:1")},
                              {"click in bravo", click(1, kText + 2), status_has("2:3")},
                              {"click past charlie's end", click(2, kText + 30), status_has("3:8")},
                              {"click on the gutter", click(0, 1), status_has("1:1")},
                              {"click below the text", click(9, kText + 2), status_has("4:1")},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}

TEST_CASE("a drag selects, and typing replaces what it selected") {
    const auto run = run_app("mouse-drag", "alpha\nbravo\n",
                             {{"start", "", status_has("1:1")},
                              {"drag over alpha", press(0, kText) + drag(0, kText + 3) + drag(0, kText + 5) + release(0, kText + 5), status_has("1:6")},
                              {"type over it", "Z", row_is(0, "  1 Z")},
                              {"quit", "\x11\x1b[C", nullptr}});
    check_steps(run);
}

TEST_CASE("a double click selects a word, a triple click the line, Shift+click extends") {
    const auto run = run_app("mouse-multi", "one two three\nfour five\n",
                             {{"start", "", status_has("1:1")},
                              {"double click on two", click(0, kText + 5) + click(0, kText + 5), status_has("1:8")},
                              {"type over the word", "X", row_is(0, "  1 one X three")},
                              {"triple click on line 2", click(1, kText + 1) + click(1, kText + 1) + click(1, kText + 1), status_has("3:1")},
                              {"type over the line", "Y", row_is(1, "  2 Y")},
                              {"click at the start, Shift+click further", click(0, kText) + click(0, kText + 3, 4), status_has("1:4")},
                              {"type over the selection", "W", row_is(0, "  1 W X three")},
                              {"quit", "\x11\x1b[C", nullptr}});
    check_steps(run);
}

TEST_CASE("the wheel scrolls the view under the pointer, not the cursor") {
    std::string text;
    for (int i = 1; i <= 40; ++i) text += std::format("line {}\n", i);
    const auto run = run_app("mouse-wheel", text,
                             {{"start", "", row_is(0, "  1 line 1")},
                              {"wheel down", wheel(3, kText, true), row_is(0, "  4 line 4")},
                              {"the cursor stays", "", status_has("1:1")},
                              {"wheel up past the top", wheel(3, kText, false) + wheel(3, kText, false), row_is(0, "  1 line 1")},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}

TEST_CASE("a click in another split gives it the focus") {
    auto marks = [](char top, char bottom) {
        return [top, bottom](const VtScreen& s) {
            const auto rows = status_rows(s, "file.txt");
            return rows.size() == 2 && rows[0].second == top && rows[1].second == bottom;
        };
    };
    const auto run = run_app("mouse-split", "one\ntwo\nthree\n",
                             {{"View > Split", "\x1bxvp", marks('>', ' ')},
                              {"click in the lower split", click(7, kText + 1), marks(' ', '>')},
                              {"and back in the upper", click(1, kText + 2), marks('>', ' ')},
                              {"the cursor went there", "", [](const VtScreen& s) { return s.row(5).starts_with(">") && s.row(5).find("2:3") != std::string::npos; }},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}

TEST_CASE("the menu takes clicks while it is shown") {
    const auto run = run_app("mouse-menu", "text\n",
                             {{"Esc shows the bar", "\x1b", [](const VtScreen& s) { return s.row(0).find("File") != std::string::npos; }},
                              {"click File", click(0, 2), [](const VtScreen& s) { return s.contains("Save As"); }},
                              {"click Save As…", click(3, 4), [](const VtScreen& s) { return s.contains(" Save As "); }},
                              {"Esc closes the dialog", "\x1b", [](const VtScreen& s) { return !s.contains(" Save As "); }},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}

TEST_CASE("with the mouse setting off, clicks do nothing") {
    const auto run = run_app("mouse-off", {File{"file.txt", "alpha\nbravo\n"}},
                             {{"start", "", status_has("1:1")},
                              {"click in bravo", click(1, kText + 2), nullptr},
                              {"the cursor stays", "\x1b[C", status_has("1:2")},
                              {"quit", "\x11", nullptr}},
                             settings_setup(R"({"mouse": false})"));
    check_steps(run);
}

TEST_CASE("view mode: Markdown laid out without line numbers, other files read-only as text") {
    const auto md = run_app("view-md", {File{"notes.md", "# Notes\n\nSome **bold** text.\n"}},
                            {{"laid out, no gutter", "", row_is(0, "Notes")},
                             {"typing is refused", "x", status_has("view mode")},
                             {"quit", "\x11", nullptr}},
                            read_only_setup());
    check_steps(md);
    const auto txt = run_app("view-txt", {File{"plain.txt", "alpha\n"}},
                             {{"the text with its numbers", "", row_is(0, "  1 alpha")},
                              {"marked [view]", "", status_has("[view]")},
                              {"typing is refused", "x", row_is(0, "  1 alpha")},
                              {"quit", "\x11", nullptr}},
                             read_only_setup());
    check_steps(txt);
    // No extension, a heading on the first line: Markdown.
    const auto bare = run_app("view-bare", {File{"NOTES", "# Heading\n\nText.\n"}},
                              {{"laid out", "", row_is(0, "Heading")}, {"quit", "\x11", nullptr}}, read_only_setup());
    check_steps(bare);
}

TEST_CASE("View > View Mode turns view mode on and off") {
    const auto run = run_app("view-toggle", {File{"notes.md", "# Notes\n\ntext\n"}},
                             {{"editing: the source", "", row_is(0, "  1 # Notes")},
                              {"View > View Mode", "\x1bxvv", row_is(0, "Notes")},
                              {"marked", "", status_has("[view]")},
                              {"and off again", "\x1bxvv", row_is(0, "  1 # Notes")},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}

namespace {

// A step condition that changes a file on disk the first time it is asked, then is met: a
// new file renamed over it, as most editors save (`in_place`: rewritten where it is).
auto change_file(const std::string& test, const std::string& name, std::optional<std::string> text, bool in_place = false) {
    auto done = std::make_shared<bool>(false);
    return [=](const VtScreen&) {
        if (!*done) {
            const fs::path p = fs::path(MOD_TEST_SCRATCH) / "app_test" / test / name;
            if (text && in_place) {
                // Overwritten from the start, never truncated: a mapped file that shrinks, even
                // for a moment, raises SIGBUS on the next read (an accepted risk; see file_map.hpp).
                // The text given must be at least as long as the file.
                std::fstream(p, std::ios::binary | std::ios::in | std::ios::out) << *text;
            } else if (text) {
                const fs::path tmp = p.string() + ".new";
                std::ofstream(tmp, std::ios::binary | std::ios::trunc) << *text;
                fs::rename(tmp, p);
            } else {
                fs::remove(p);
            }
            *done = true;
        }
        return true;
    };
}

}  // namespace

TEST_CASE("a file changed on disk with no unsaved changes is reloaded, and the status line says so") {
    const auto run = run_app("reload-auto", "old text\n",
                             {{"start", "", row_is(0, "  1 old text")},
                              {"another program rewrites it", "", change_file("reload-auto", "file.txt", "new text, longer\n")},
                              {"reloaded", "", row_is(0, "  1 new text, longer")},
                              {"said so", "", status_has("reloaded file.txt")},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}

TEST_CASE("with unsaved changes, or auto_reload off, a change on disk leaves a notice until File > Reload") {
    const auto dirty = run_app("reload-dirty", "old\n",
                               {{"start", "", row_is(0, "  1 old")},
                                {"an edit", "x", row_is(0, "  1 xold")},
                                {"the file changes", "", change_file("reload-dirty", "file.txt", "theirs, longer\n")},
                                {"a notice, the edit kept", "", [](const VtScreen& s) {
                                     return s.status().find("changed on disk") != std::string::npos && s.row(0).starts_with("  1 xold");
                                 }},
                                {"typing still works: nothing took the focus", "y", row_is(0, "  1 xyold")},
                                {"quit, discarding", "\x11\x1b[C\r", nullptr}});
    check_steps(dirty);
    const auto off = run_app("reload-off", {File{"file.txt", "old\n"}},
                             {{"start", "", row_is(0, "  1 old")},
                              {"the file changes", "", change_file("reload-off", "file.txt", "new, longer\n")},
                              {"a lasting notice", "", status_has("File > Reload")},
                              {"File > Reload", "\x1bxfr", row_is(0, "  1 new, longer")},
                              {"the notice is gone", "", [](const VtScreen& s) { return s.status().find("changed on disk") == std::string::npos; }},
                              {"quit", "\x11", nullptr}},
                             settings_setup(R"({"auto_reload": false})"));
    check_steps(off);
}

TEST_CASE("a deleted file gets a notice; a hidden document's reload is told when it is shown") {
    const auto gone = run_app("reload-deleted", "text\n",
                              {{"start", "", row_is(0, "  1 text")},
                               {"deleted", "", change_file("reload-deleted", "file.txt", std::nullopt)},
                               {"a notice", "", status_has("deleted on disk")},
                               {"quit", "\x11\x1b[C\r", nullptr}});
    check_steps(gone);
    const auto hidden = run_app("reload-hidden", {File{"a.txt", "alpha\n"}, File{"b.txt", "beta\n"}},
                                {{"a shown", "", row_is(0, "  1 alpha")},
                                 {"b changes on disk", "", change_file("reload-hidden", "b.txt", "beta two\n")},
                                 {"a's status line says nothing of it", "", [](const VtScreen& s) {
                                      std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                      return s.status().find("b.txt") == std::string::npos;
                                  }},
                                 {"Documents > b.txt: reloaded, and told", "\x1bxd2", [](const VtScreen& s) {
                                      return s.row(0).starts_with("  1 beta two") && s.status().find("reloaded b.txt") != std::string::npos;
                                  }},
                                 {"quit", "\x11", nullptr}});
    check_steps(hidden);
}

TEST_CASE("a mapped file rewritten in place is reloaded even with unsaved changes, as they cannot be kept") {
    if (!maps_files()) return;  // read in (Windows): an ordinary change
    const auto run = run_app("reload-in-place", "old\n",
                             {{"start", "", row_is(0, "  1 old")},
                              {"an edit", "x", row_is(0, "  1 xold")},
                              {"rewritten in place", "", change_file("reload-in-place", "file.txt", "theirs, longer\n", true)},
                              {"reloaded, and told why", "", [](const VtScreen& s) {
                                   return s.row(0).starts_with("  1 theirs, longer") && s.status().find("could not be kept") != std::string::npos;
                               }},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}

TEST_CASE("End goes to the end of the row on screen, and twice quickly to the line's end; Home the same") {
    // 80 columns, a gutter of 4: rows of 75 columns.
    std::string words;
    while (words.size() < 150) words += "word ";
    const auto run = run_app("visual-end", words + "\n",
                             {{"start", "", status_has("1:1")},
                              {"End: the row's end", "\x1b[F", status_has("1:75")},
                              {"End twice quickly: the line's end", "\x1b[F\x1b[F", status_has(std::format("1:{}", words.size() + 1))},
                              {"Home: the last row's start", "\x1b[H", status_has("1:76")},
                              {"Home twice quickly: the line's start", "\x1b[H\x1b[H", status_has("1:1")},
                              {"quit", "\x11", nullptr}});
    check_steps(run);
}
