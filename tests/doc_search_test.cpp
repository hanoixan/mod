#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "app/doc_search.hpp"
#include "platform/terminal.hpp"
#include "ui/doc_search_view.hpp"
#include "ui/screen.hpp"
#include "ui/theme.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "doc_search_test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_file(const fs::path& p, std::string_view bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// A small manual: index.md, two pages and a page in a subfolder.
fs::path manual(const std::string& name) {
    const fs::path root = scratch(name);
    write_file(root / "index.md", "# Manual\nSee [undo](undo.md) and [keys](keys.md).\n");
    write_file(root / "undo.md", "# Undo history\nPrune History removes old steps.\nprune it with P.\n");
    write_file(root / "keys.md", "# Keys\nCtrl+K cuts.\n");
    write_file(root / "more" / "deep.md", "# Deep\nPRUNE in capitals.\n");
    write_file(root / "notes.txt", "prune in a file that is not a page\n");
    return root;
}

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

std::string screen_row(const Screen& s, int row) {
    std::string out;
    for (int c = 0; c < s.cols(); ++c) {
        const Cell& cell = s.cell(row, c);
        out.append(cell.utf8.data(), cell.len);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

}  // namespace

TEST_CASE("the pages: every .md under the root, index.md first, the rest in path order") {
    const fs::path root = manual("pages");
    const auto pages = doc_pages(root);
    REQUIRE(pages.size() == 4);
    CHECK(pages[0] == fs::path("index.md"));
    CHECK(pages[1] == fs::path("keys.md"));
    CHECK(pages[2] == fs::path("more/deep.md"));
    CHECK(pages[3] == fs::path("undo.md"));
}

TEST_CASE("search: every match in every page, ignoring case, with line, column and the line's text") {
    const fs::path root = manual("search");
    const DocSearchResult r = search_docs(root, "prune");
    REQUIRE(r.matches.size() == 3);
    CHECK_FALSE(r.capped);
    CHECK(r.matches[0].page == fs::path("more/deep.md"));
    CHECK(r.matches[0].line == 2);
    CHECK(r.matches[0].column == 1);
    CHECK(r.matches[0].excerpt == "PRUNE in capitals.");
    CHECK(r.matches[1].page == fs::path("undo.md"));
    CHECK(r.matches[1].line == 2);
    CHECK(r.matches[1].offset == 15);
    CHECK(r.matches[2].line == 3);
    CHECK(r.matches[2].column == 1);
    CHECK(search_docs(root, "").matches.empty());
    CHECK(search_docs(root, "nothing like this").matches.empty());
    CHECK(search_docs(root, "ctrl+k").matches.size() == 1);  // no pattern syntax: plain text
}

TEST_CASE("search stops at the cap and says so") {
    const fs::path root = scratch("cap");
    std::string many;
    for (int i = 0; i < 30; ++i) many += "word word\n";
    write_file(root / "index.md", many);
    const DocSearchResult r = search_docs(root, "word", 25);
    CHECK(r.matches.size() == 25);
    CHECK(r.capped);
}

TEST_CASE("finding the manual: $MOD_DOC_DIR, then beside the binary, then the install path, then the source tree") {
    const fs::path base = scratch("find");
    const fs::path env = base / "env";
    const fs::path exe_root = base / "prefix";
    const fs::path installed = base / "installed";
    const fs::path source = base / "source";
    for (const fs::path& d : {env, exe_root / "share" / "mod" / "doc", installed, source}) write_file(d / "index.md", "# x\n");
    fs::create_directories(exe_root / "bin");
    DocDirSources s{env, exe_root / "bin" / "mod", installed, source};
    CHECK(find_doc_dir(s) == env);
    s.env = base / "missing";  // no index.md there
    CHECK(find_doc_dir(s) == exe_root / "share" / "mod" / "doc");
    s.exe = base / "elsewhere" / "bin" / "mod";
    CHECK(find_doc_dir(s) == installed);
    s.installed = base / "nope";
    CHECK(find_doc_dir(s) == source);
    s.source = base / "none";
    CHECK_FALSE(find_doc_dir(s).has_value());
    s.env.reset();
    const auto looked = doc_dir_candidates(s);
    REQUIRE(looked.size() == 3);
    CHECK(looked[0] == base / "elsewhere" / "share" / "mod" / "doc");
}

TEST_CASE("the search panel: typing searches, the list shows page:line and the text, Enter opens the selected match") {
    const fs::path root = manual("panel");
    DocSearchView v;
    v.open([&](std::string_view q) { return search_docs(root, q); });
    CHECK(v.is_open());
    CHECK(v.query().empty());
    for (const char c : std::string_view("prune")) v.handle_key(ch(static_cast<char32_t>(c)));
    CHECK(v.query() == "prune");
    REQUIRE(v.results().matches.size() == 3);
    CHECK(v.row_text(0) == "more/deep.md:2  PRUNE in capitals.");
    v.handle_key(key(Key::Down));
    CHECK(v.selected() == 1);
    const DocSearchKeyResult r = v.handle_key(key(Key::Enter));
    REQUIRE(r.open.has_value());
    CHECK(r.open->page == fs::path("undo.md"));
    CHECK(r.open->line == 2);
    CHECK(r.closed);
    CHECK_FALSE(v.is_open());
}

TEST_CASE("the search panel keeps its query, results and selection when it is opened again") {
    const fs::path root = manual("keep");
    int searches = 0;
    DocSearchView v;
    const auto search = [&](std::string_view q) {
        ++searches;
        return search_docs(root, q);
    };
    v.open(search);
    for (const char c : std::string_view("prune")) v.handle_key(ch(static_cast<char32_t>(c)));
    v.handle_key(key(Key::End));
    const DocSearchKeyResult esc = v.handle_key(key(Key::Escape));
    CHECK(esc.closed);
    CHECK_FALSE(esc.open.has_value());
    const int before = searches;
    v.open(search);
    CHECK(v.query() == "prune");
    CHECK(v.results().matches.size() == 3);
    CHECK(v.selected() == 2);
    CHECK(searches == before);  // nothing is searched again on reopening
    v.handle_key(key(Key::Backspace, kCtrl));  // clears the query
    CHECK(v.query().empty());
    CHECK(v.results().matches.empty());
    v.handle_paste("Ctrl+K\nrest");
    CHECK(v.query() == "Ctrl+K");
    CHECK(v.results().matches.size() == 1);
}

TEST_CASE("the search panel draws the field, the count, and the rows with the selection highlighted") {
    const fs::path root = manual("render");
    DocSearchView v;
    v.open([&](std::string_view q) { return search_docs(root, q); });
    for (const char c : std::string_view("prune")) v.handle_key(ch(static_cast<char32_t>(c)));
    NullTerminal term;
    Screen screen(term);
    screen.resize({10, 60});
    v.render(screen, Rect{1, 1, 8, 58});
    CHECK(screen_row(screen, 1).find("Search the manual: prune") != std::string::npos);
    CHECK(screen_row(screen, 1).find("3 matches") != std::string::npos);
    CHECK(screen_row(screen, 2).find("more/deep.md:2") != std::string::npos);
    CHECK(screen.cell(2, 1).attr == attr_for(Style::list_selected));
    CHECK(screen_row(screen, 3).find("undo.md:2") != std::string::npos);
    CHECK(screen_row(screen, 0).empty());
    v.handle_key(key(Key::Backspace, kCtrl));
    for (const char c : std::string_view("zzz")) v.handle_key(ch(static_cast<char32_t>(c)));
    v.render(screen, Rect{1, 1, 8, 58});
    CHECK(screen_row(screen, 2).find("no matches") != std::string::npos);
}
