#include <chrono>
#include <doctest/doctest.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "edit/clipboard.hpp"
#include "edit/document.hpp"
#include "edit/editor.hpp"
#include "edit/sidecar.hpp"
#include "search/regex.hpp"
#include "search/search.hpp"
#include "util/event_queue.hpp"
#include "time_budget.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "search_test";
    fs::create_directories(dir);
    const fs::path p = dir / name;
    fs::remove(p);
    fs::remove(sidecar_path_for(p));
    return p;
}

void write_file(const fs::path& p, std::string_view bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

struct Fixture {
    EventQueue q{{}};
    Clipboard clipboard;
    std::unique_ptr<Document> doc;
    std::unique_ptr<Editor> ed;
    std::unique_ptr<Searcher> s;

    Fixture(const std::string& name, std::string_view text) {
        const fs::path p = scratch(name);
        write_file(p, text);
        auto d = Document::open(p, q);
        REQUIRE(d);
        doc = std::move(*d);
        ed = std::make_unique<Editor>(*doc, clipboard);
        s = std::make_unique<Searcher>(*doc, *ed);
    }
    ~Fixture() {
        s.reset();
        ed.reset();
    }

    std::string text() const { return doc->text().read(0, doc->text().size()); }

    StepResult run() {
        for (;;) {
            StepResult r = s->step();
            if (r.kind != StepResult::in_progress) return r;
        }
    }
    // Every match, front to back, as (start, end), using repeated F3 without wrap.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> all(std::string_view pattern, SearchOptions o = {}) {
        o.wrap = false;
        REQUIRE(s->set_query(pattern, o));
        std::vector<std::pair<std::uint64_t, std::uint64_t>> out;
        ed->select_range(0, 0);
        for (;;) {
            s->find_next(ed->cursor(), Direction::forward);
            const StepResult r = run();
            if (r.kind != StepResult::found) break;
            out.emplace_back(r.match.start, r.match.end);
            if (out.size() > 1000) break;
        }
        return out;
    }
};

using Spans = std::vector<std::pair<std::uint64_t, std::uint64_t>>;

Regex compile(std::string_view pattern, RegexOptions o = {}) {
    auto r = Regex::compile(pattern, o);
    REQUIRE(r);
    return std::move(*r);
}

std::string read_from(std::string_view text, std::uint64_t s, std::uint64_t e) { return std::string(text.substr(s, e - s)); }

}  // namespace

TEST_CASE("matches across piece boundaries") {
    Fixture f("pieces.txt", "abc needle def\n");
    f.ed->select_range(6, 6);
    f.ed->insert_text("XX");  // "abc neXXedle def": the match now spans three pieces
    CHECK(f.all("neXXedle") == Spans{{4, 12}});
}

TEST_CASE("matches across window boundaries, and lines longer than the window") {
    std::string text;
    const std::string line(99, '.');
    while (text.size() < (4u << 20) - 300) text += line + "\n";
    text += std::string((4u << 20) - 3 - text.size(), '.') + "NEEDLE" + std::string(46, '.') + "\n";  // straddles 4 MiB
    const std::uint64_t first = text.find("NEEDLE");
    REQUIRE(first < (4u << 20));
    REQUIRE(first + 6 > (4u << 20));
    // A 6 MiB line with a match at its end: the window must grow.
    text += std::string(6u << 20, 'x') + "NEEDLE\n";
    const std::uint64_t second = text.rfind("NEEDLE");
    Fixture f("windows.txt", text);
    CHECK(f.all("NEEDLE") == Spans{{first, first + 6}, {second, second + 6}});
    REQUIRE(f.s->set_query("x+NEEDLE", {}));
    f.s->find_next(0, Direction::forward);
    const StepResult r = f.run();
    REQUIRE(r.kind == StepResult::found);
    CHECK(r.match.end == second + 6);
    CHECK(r.match.start == second - (6u << 20));
    CHECK_FALSE(r.line_too_long);
}

TEST_CASE("a line longer than 16 MiB is searched in pieces and reported") {
    std::string text = std::string(20u << 20, 'y') + "TAIL\n";
    Fixture f("toolong.txt", text);
    REQUIRE(f.s->set_query("TAIL", {}));
    f.s->find_next(0, Direction::forward);
    const StepResult r = f.run();
    REQUIRE(r.kind == StepResult::found);
    CHECK(r.match.start == 20u << 20);
    CHECK(r.line_too_long);
}

TEST_CASE("backward search and wrap") {
    Fixture f("back.txt", "one two one two one\n");
    REQUIRE(f.s->set_query("one", {}));
    f.ed->select_range(10, 10);
    f.s->find_next(10, Direction::backward);
    StepResult r = f.run();
    REQUIRE(r.kind == StepResult::found);
    CHECK(r.match.start == 8);
    CHECK(f.ed->selection() == std::pair<std::uint64_t, std::uint64_t>{8, 11});
    f.s->find_next(f.ed->cursor(), Direction::backward);  // from the current match: steps over it
    r = f.run();
    CHECK(r.match.start == 0);
    CHECK_FALSE(r.wrapped);
    f.s->find_next(f.ed->cursor(), Direction::backward);
    r = f.run();
    REQUIRE(r.kind == StepResult::found);
    CHECK(r.match.start == 16);
    CHECK(r.wrapped);

    f.s->find_next(f.ed->cursor(), Direction::forward);
    r = f.run();
    REQUIRE(r.kind == StepResult::found);
    CHECK(r.match.start == 0);
    CHECK(r.wrapped);

    SUBCASE("without wrap the search stops at the end") {
        SearchOptions o;
        o.wrap = false;
        REQUIRE(f.s->set_query("one", o));
        f.ed->select_range(17, 17);
        f.s->find_next(17, Direction::forward);
        CHECK(f.run().kind == StepResult::not_found);
    }
    SUBCASE("a single match is found again after one full lap, then reported once") {
        REQUIRE(f.s->set_query("two one\\n", {}));
        f.s->find_next(0, Direction::forward);
        CHECK(f.run().kind == StepResult::not_found);  // "\n" never matches: lines are separate
        REQUIRE(f.s->set_query("two one$", {}));
        f.s->find_next(0, Direction::forward);
        r = f.run();
        REQUIRE(r.kind == StepResult::found);
        const std::uint64_t at = r.match.start;
        f.s->find_next(f.ed->cursor(), Direction::forward);
        r = f.run();
        REQUIRE(r.kind == StepResult::found);
        CHECK(r.match.start == at);
        CHECK(r.wrapped);
    }
}

TEST_CASE("whole-word and case options, Unicode-aware") {
    Fixture f("options.txt", "Cat cat concat CAT \xC3\x89t\xC3\xA9 \xC3\xA9t\xC3\xA9 x\xC3\xA9t\xC3\xA9\n");
    SearchOptions o;
    CHECK(f.all("cat", o) == Spans{{4, 7}, {11, 14}});
    o.case_insensitive = true;
    CHECK(f.all("cat", o).size() == 4);
    o.whole_word = true;
    CHECK(f.all("cat", o) == Spans{{0, 3}, {4, 7}, {15, 18}});
    // É matches é case-insensitively, and \b treats é as a word character.
    CHECK(f.all("\xC3\xA9t\xC3\xA9", o) == Spans{{19, 24}, {25, 30}});
    o.case_insensitive = false;
    o.whole_word = false;
    CHECK(f.all("\\w+", o).size() == 7);
    CHECK(f.all("\\bt", o).empty());  // "t" inside "été" is not at a word boundary
    o.regex = false;
    o.whole_word = true;
    CHECK(f.all("c.t", o).empty());  // literal and whole word: the dot is escaped
}

TEST_CASE("line-bounded matching") {
    Fixture f("lines.txt", "a b\r\nc  d\nab\n\nend");
    CHECK(f.all("\\s+") == Spans{{1, 2}, {6, 8}});  // never the CR LF or LF
    CHECK(f.all("[^a]+") == Spans{{1, 3}, {5, 9}, {11, 12}, {14, 17}});
    CHECK(f.all("\\R").empty());
    CHECK(f.all("a\\nb").empty());
    CHECK(f.all("b\\r").empty());
    Spans nonempty;
    for (const auto& [a, b] : f.all(".*")) {
        CHECK(f.text().substr(a, b - a).find_first_of("\r\n") == std::string::npos);
        if (b > a) nonempty.emplace_back(a, b);
    }
    CHECK(nonempty == Spans{{0, 3}, {5, 9}, {10, 12}, {14, 17}});  // each line's text, never its break
    CHECK(f.all("^") == Spans{{0, 0}, {5, 5}, {10, 10}, {13, 13}, {14, 14}});
    CHECK(f.all("$") == Spans{{3, 3}, {9, 9}, {12, 12}, {13, 13}, {17, 17}});
}

TEST_CASE("invalid patterns are errors") {
    Fixture f("invalid.txt", "x\n");
    const Status s = f.s->set_query("a(b", {});
    REQUIRE_FALSE(s);
    CHECK(s.error().code == ErrorCode::regex);
    CHECK(s.error().message.find("offset") != std::string::npos);
    CHECK_FALSE(f.s->has_query());
    CHECK(f.s->set_query("a(b", SearchOptions{false, false, false, true}));  // fine as plain text
}

TEST_CASE("search_window: partial matches at the window end and invalid UTF-8") {
    const Regex r = compile("ab+c");
    const std::string w = "xx\nxxabb";
    auto res = r.search_window(std::as_bytes(std::span(w.data(), w.size())), 100, 100, false);
    REQUIRE(res);
    CHECK(res->kind == WindowResult::need_more);
    res = r.search_window(std::as_bytes(std::span(w.data(), w.size())), 100, 100, true);
    CHECK(res->kind == WindowResult::none);
    const std::string bad = "\xFF\xFE" "abc\xC3";
    res = r.search_window(std::as_bytes(std::span(bad.data(), bad.size())), 0, 0, true);
    REQUIRE(res);
    REQUIRE(res->kind == WindowResult::found);
    CHECK(res->match.start == 2);
    CHECK(res->match.end == 5);
}

TEST_CASE("replacement templates") {
    const std::string text = "2024-06-01 x";
    const Regex r = compile("(?<year>\\d+)-(\\d+)-(\\d+)( y)?(a)?(b)?(c)?(d)?(e)?(f)?");
    auto res = r.search_window(std::as_bytes(std::span(text.data(), text.size())), 0, 0, true);
    REQUIRE(res);
    REQUIRE(res->kind == WindowResult::found);
    const Match& m = res->match;
    auto read = [&](std::uint64_t s, std::uint64_t e) { return read_from(text, s, e); };
    CHECK(*r.expand_replacement(m, "$3.$2.$1", read) == "01.06.2024");
    CHECK(*r.expand_replacement(m, "${year}/${2}0", read) == "2024/060");
    CHECK(*r.expand_replacement(m, "[$0] [${0}] $$5", read) == "[2024-06-01] [2024-06-01] $5");
    CHECK(*r.expand_replacement(m, "<$4|${10}>", read) == "<|>");  // non-participating groups
    CHECK(*r.expand_replacement(m, "\\1", read) == "\\1");
    CHECK(r.expand_replacement(m, "$11", read).error().message == "no group 11");
    CHECK(r.expand_replacement(m, "${month}", read).error().message == "no group named 'month'");
    CHECK(r.expand_replacement(m, "${1", read).error().code == ErrorCode::regex);
    CHECK(r.expand_replacement(m, "$x", read).error().code == ErrorCode::regex);
    CHECK(r.expand_replacement(m, "end$", read).error().code == ErrorCode::regex);

    SUBCASE("plain-text mode replaces with literal text") {
        const Regex lit = compile("$5", RegexOptions{false, true, false});
        const std::string t = "costs $5";
        auto found = lit.search_window(std::as_bytes(std::span(t.data(), t.size())), 0, 0, true);
        REQUIRE(found->kind == WindowResult::found);
        CHECK(found->match.start == 6);
        auto rd = [&](std::uint64_t s, std::uint64_t e) { return read_from(t, s, e); };
        CHECK(*lit.expand_replacement(found->match, "$5.00 and $1 and ${x", rd) == "$5.00 and $1 and ${x");
    }
}

TEST_CASE("replace_current and replace_all") {
    Fixture f("replace.txt", "a1 b22 c333\nd4\n");
    REQUIRE(f.s->set_query("([a-z])(\\d+)", {}));

    SUBCASE("replace_current replaces the selected match and finds the next") {
        CHECK(f.s->replace_current("x").error().code == ErrorCode::canceled);  // nothing selected yet
        f.s->find_next(0, Direction::forward);
        REQUIRE(f.run().kind == StepResult::found);
        REQUIRE(f.s->replace_current("$2$1"));
        CHECK(f.text() == "1a b22 c333\nd4\n");
        const StepResult r = f.run();
        REQUIRE(r.kind == StepResult::found);
        CHECK(r.match.start == 3);
    }
    SUBCASE("replace_all is one undo node") {
        REQUIRE(f.s->replace_all("<$2$1>"));
        const StepResult r = f.run();
        REQUIRE(r.kind == StepResult::replaced);
        CHECK(r.count == 4);
        CHECK(f.text() == "<1a> <22b> <333c>\n<4d>\n");
        REQUIRE(f.ed->undo());
        CHECK(f.text() == "a1 b22 c333\nd4\n");
        REQUIRE(f.ed->redo());
        CHECK(f.text() == "<1a> <22b> <333c>\n<4d>\n");
    }
    SUBCASE("a template error is reported before anything is replaced") {
        CHECK(f.s->replace_all("$7").error().message == "no group 7");
        CHECK_FALSE(f.s->active());
        CHECK(f.text() == "a1 b22 c333\nd4\n");
    }
    SUBCASE("empty matches advance by one code point") {
        REQUIRE(f.s->set_query("x*", {}));
        Fixture g("empty.txt", "\xC3\xA9z\n");
        REQUIRE(g.s->set_query("x*", {}));
        REQUIRE(g.s->replace_all("-"));
        const StepResult r = g.run();
        REQUIRE(r.kind == StepResult::replaced);
        CHECK(g.text() == "-\xC3\xA9-z-\n-");
    }
}

TEST_CASE("canceling a replace-all keeps what was replaced as one node") {
    std::string text;
    for (int i = 0; i < 6000; ++i) text += "word" + std::string(995, '.') + "\n";  // 6 MB: more than one window
    Fixture f("cancel.txt", text);
    REQUIRE(f.s->set_query("word", {}));
    REQUIRE(f.s->replace_all("w"));
    REQUIRE(f.s->step(64 * 1024).kind == StepResult::in_progress);  // a small slice
    f.s->cancel();
    CHECK_FALSE(f.s->active());
    const std::string after = f.text();
    CHECK(after.size() < text.size());
    CHECK(after.ends_with("word" + std::string(995, '.') + "\n"));
    REQUIRE(f.ed->undo());
    CHECK(f.text() == text);
}

TEST_CASE("a multi-GB sparse file is searched within the time-slice budget") {
    const fs::path p = scratch("sparse.bin");
    {
        const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        REQUIRE(fd >= 0);
        REQUIRE(::ftruncate(fd, (2ll << 30) - 7) == 0);
        REQUIRE(::pwrite(fd, "needle\n", 7, (2ll << 30) - 7) == 7);
        ::close(fd);
    }
    EventQueue q({});
    Clipboard clipboard;
    auto d = Document::open(p, q);
    REQUIRE(d);
    Editor ed(**d, clipboard);
    Searcher s(**d, ed);
    REQUIRE(s.set_query("needle", {}));
    s.find_next(0, Direction::forward);
    for (int i = 0; i < 3; ++i) {
        const StepResult r = s.step();  // about 8 MiB each, never the whole file
        CHECK(r.kind == StepResult::in_progress);
        CHECK(r.line_too_long);
    }
    s.cancel();
    fs::remove(p);
}

TEST_CASE("whole word: a term that starts or ends with punctuation still matches on its own") {
    Fixture f("punct.txt", "call foo( x) and foo(y; -x and a-x\n");
    SearchOptions o;
    o.regex = false;
    o.whole_word = true;
    CHECK(f.all("foo(", o) == Spans{{5, 9}});  // "foo(y" is glued to the y
    CHECK(f.all("-x", o) == Spans{{24, 26}});  // in "a-x" it is glued to a word character
    CHECK(f.all("x", o) == Spans{{10, 11}, {25, 26}, {33, 34}});
}

TEST_CASE("Replace All over one long line replaces every match") {
    std::string line;
    while (line.size() < 200'000) line += "a b ";
    Fixture f("long_line.txt", line + "\n");
    SearchOptions o;
    o.regex = false;
    REQUIRE(f.s->set_query("a", o));
    REQUIRE(f.s->replace_all("c"));
    const StepResult r = f.run();
    CHECK(r.kind != StepResult::in_progress);
    CHECK(f.text().find('a') == std::string::npos);
    CHECK(f.text().size() == line.size() + 1);
}

TEST_CASE("searching one very long line match after match takes linear time") {
    std::string line;
    while (line.size() < 4'000'000) line += "a b ";
    const Regex re = compile("a", RegexOptions{.literal = true});
    const auto window = std::as_bytes(std::span(line.data(), line.size()));
    LineCursor cursor;
    std::uint64_t from = 0;
    std::size_t found = 0;
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        auto r = re.search_window(window, 0, from, true, &cursor);
        REQUIRE(r);
        if (r->kind != WindowResult::found) break;
        ++found;
        from = r->match.end;
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(found == line.size() / 4);
    CHECK(seconds < time_budget(10.0));
}
