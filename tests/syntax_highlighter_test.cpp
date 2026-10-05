#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "edit/document.hpp"
#include "edit/sidecar.hpp"
#include "syntax/language_config.hpp"
#include "syntax/layered_highlighter.hpp"
#include "syntax/syntax_highlighter.hpp"
#include "util/event_queue.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

using Items = std::vector<std::tuple<std::string, Style>>;

// The text and style of each span `scan_line` gives for one line from the plain state.
Items styles(std::string_view line, const SyntaxSpec& spec) {
    Items out;
    for (const StyleSpan& s : SyntaxHighlighter::scan_line(spec, {}, line).spans)
        out.emplace_back(std::string(line.substr(s.start, s.end - s.start)), s.style);
    return out;
}

SyntaxSpec py() {
    SyntaxSpec s;
    s.keywords = {"def", "return", "if"};
    s.constants = {"None", "True"};
    s.line_comments = {"#"};
    s.strings = {{"\"\"\"", "\"\"\"", '\\', true, {}}, {"\"", "\"", '\\', false, {}}, {"'", "'", '\\', false, {}}};
    s.string_prefixes = {"f", "r", "rb"};
    return s;
}

SyntaxSpec c_like() {
    SyntaxSpec s;
    s.keywords = {"int", "return"};
    s.line_comments = {"//"};
    s.block_comments = {{"/*", "*/"}};
    s.strings = {{"\"", "\"", '\\', false, {}}};
    return s;
}

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "syntax_highlighter_test";
    fs::create_directories(dir);
    const fs::path p = dir / name;
    fs::remove(p);
    fs::remove(sidecar_path_for(p));
    return p;
}

struct Fixture {
    EventQueue q{{}};
    std::string text;
    std::unique_ptr<Document> doc;
    std::unique_ptr<SyntaxHighlighter> hl;

    Fixture(const std::string& name, std::string content, SyntaxSpec spec) : text(std::move(content)) {
        const fs::path p = scratch(name);
        std::ofstream(p, std::ios::binary) << text;
        auto d = Document::open(p, q);
        REQUIRE(d);
        doc = std::move(*d);
        hl = std::make_unique<SyntaxHighlighter>(*doc, std::move(spec));
        doc->add_listener(hl.get());
    }
    ~Fixture() {
        if (doc) doc->remove_listener(hl.get());
    }

    std::uint64_t line_start(std::size_t n) const {
        std::uint64_t off = 0;
        for (std::size_t i = 0; i < n; ++i) off = text.find('\n', off) + 1;
        return off;
    }
    // The styles of line `n`'s spans, checked to be sorted, inside the line and absolute.
    std::vector<Style> line_styles(std::size_t n) {
        const std::uint64_t s = line_start(n);
        const std::size_t e = text.find('\n', s);
        const std::string l = text.substr(s, e == std::string::npos ? std::string::npos : e - s);
        std::vector<Style> out;
        const auto spans = hl->spans_for_line(s, l);
        for (std::size_t i = 0; i < spans.size(); ++i) {
            CHECK(spans[i].start >= s);
            CHECK(spans[i].end <= s + l.size());
            CHECK(spans[i].start < spans[i].end);
            if (i > 0) CHECK(spans[i - 1].end <= spans[i].start);
            out.push_back(spans[i].style);
        }
        return out;
    }
    void edit(std::uint64_t offset, std::uint64_t remove, std::string_view insert) {
        doc->apply(offset, remove, InsertContent(insert), EditKind::other, offset, offset + insert.size());
        text.replace(offset, remove, insert);
    }
};

}  // namespace

TEST_CASE("words: keywords and constants only as whole words") {
    CHECK(styles("def define(x): return None", py()) ==
          Items{{"def", Style::lsp_keyword}, {"return", Style::lsp_keyword}, {"None", Style::constant}});
    CHECK(styles("_if if_ if2 éif", py()).empty());
}

TEST_CASE("comments, and markers inside strings ignored") {
    CHECK(styles("x = '#no' # yes", py()) == Items{{"'#no'", Style::lsp_string}, {"# yes", Style::lsp_comment}});
    CHECK(styles("s = \"/* no */\" // c", c_like()) == Items{{"\"/* no */\"", Style::lsp_string}, {"// c", Style::lsp_comment}});
    CHECK(styles("a /* \"no\" */ b", c_like()) == Items{{"/* \"no\" */", Style::lsp_comment}});
}

TEST_CASE("strings: escapes, prefixes, the longest delimiter, unclosed to the end") {
    CHECK(styles(R"(f"a\"b" rb'c' xf"d")", py()) ==
          Items{{R"(f"a\"b")", Style::lsp_string}, {"rb'c'", Style::lsp_string}, {R"("d")", Style::lsp_string}});
    CHECK(styles(R"("open)", py()) == Items{{R"("open)", Style::lsp_string}});
    CHECK(styles(R"(x = """a" b""" y)", py()) == Items{{R"("""a" b""")", Style::lsp_string}});
    CHECK(styles(R"("a\\" b)", py()) == Items{{R"("a\\")", Style::lsp_string}});  // an escaped escape
    CHECK(styles(R"(f)", py()).empty());  // a prefix alone is a plain word
}

TEST_CASE("multi-line strings and block comments carry across lines") {
    const auto a = SyntaxHighlighter::scan_line(c_like(), {}, "x /* one");
    CHECK(a.end.block_comment == 0);
    REQUIRE(a.spans.size() == 1);
    CHECK(a.spans[0] == StyleSpan{2, 8, Style::lsp_comment, 0});
    const auto mid = SyntaxHighlighter::scan_line(c_like(), a.end, "int middle");
    CHECK(mid.end.block_comment == 0);
    CHECK(mid.spans == std::vector<StyleSpan>{{0, 10, Style::lsp_comment, 0}});
    const auto b = SyntaxHighlighter::scan_line(c_like(), a.end, "two */ int y");
    CHECK(b.end == SyntaxState{});
    CHECK(b.spans == std::vector<StyleSpan>{{0, 6, Style::lsp_comment, 0}, {7, 10, Style::lsp_keyword, 0}});

    const auto s = SyntaxHighlighter::scan_line(py(), {}, R"(x = """doc)");
    CHECK(s.end.string == 0);
    const auto s2 = SyntaxHighlighter::scan_line(py(), s.end, R"(more \""" still""" def)");
    CHECK(s2.end == SyntaxState{});
    CHECK(s2.spans == std::vector<StyleSpan>{{0, 18, Style::lsp_string, 0}, {19, 22, Style::lsp_keyword, 0}});
    // A single-line string does not carry.
    CHECK(SyntaxHighlighter::scan_line(py(), {}, R"(x = "open)").end == SyntaxState{});
}

TEST_CASE("maxLength refuses a lifetime-like quote; commentNeedsSpace") {
    SyntaxSpec r;
    r.strings = {{"'", "'", '\\', false, 4}};
    CHECK(styles("fn f<'a>(c: char) = 'x'", r) == Items{{"'x'", Style::lsp_string}});
    CHECK(styles(R"('\n')", r) == Items{{R"('\n')", Style::lsp_string}});
    CHECK(styles("'a", r).empty());  // unclosed and too long to be one: not a string
    SyntaxSpec sh;
    sh.line_comments = {"#"};
    sh.comment_needs_space = true;
    CHECK(styles("echo ${#v} # c", sh) == Items{{"# c", Style::lsp_comment}});
    CHECK(styles("# at the start", sh) == Items{{"# at the start", Style::lsp_comment}});
    CHECK(styles("x\t#tab", sh) == Items{{"#tab", Style::lsp_comment}});
}

TEST_CASE("numbers in their forms, and not inside words") {
    CHECK(styles("x1 = 0x1F + 1_000 + 3.5e-2 + 10u", SyntaxSpec{}) ==
          Items{{"0x1F", Style::lsp_number}, {"1_000", Style::lsp_number}, {"3.5e-2", Style::lsp_number}, {"10u", Style::lsp_number}});
    SyntaxSpec off;
    off.numbers = false;
    CHECK(styles("x = 12", off).empty());
}

TEST_CASE("case-insensitive words") {
    SyntaxSpec c;
    c.keywords = {"endif"};
    c.constants = {"ON"};
    c.case_sensitive = false;
    CHECK(styles("ENDIF(on) EndIf", c) == Items{{"ENDIF", Style::lsp_keyword}, {"on", Style::constant}, {"EndIf", Style::lsp_keyword}});
    CHECK(styles("ENDIF", py()).empty());  // case-sensitive by default
}

TEST_CASE("the longest marker at a position wins") {
    SyntaxSpec lua;
    lua.line_comments = {"--"};
    lua.block_comments = {{"--[[", "]]"}};
    const auto a = SyntaxHighlighter::scan_line(lua, {}, "x --[[ a ]] y -- z");
    CHECK(a.spans == std::vector<StyleSpan>{{2, 11, Style::lsp_comment, 0}, {14, 18, Style::lsp_comment, 0}});
    CHECK(a.end == SyntaxState{});
}

TEST_CASE("over a document: state through checkpoints, dropped after an edit before them") {
    std::string content = "/*\n";
    for (int i = 0; i < 600; ++i) content += "int x;\n";
    content += "*/\nint y;\n";
    Fixture f("doc.c", content, c_like());
    CHECK(f.line_styles(601) == std::vector<Style>{Style::lsp_comment});  // "*/" closes the comment
    CHECK(f.line_styles(602) == std::vector<Style>{Style::lsp_keyword});  // "int y;" is code
    CHECK(f.line_styles(300) == std::vector<Style>{Style::lsp_comment});  // inside, read through a checkpoint
    f.edit(0, 2, "");  // the opening "/*" goes
    CHECK(f.line_styles(300) == std::vector<Style>{Style::lsp_keyword});
    CHECK(f.line_styles(601).empty());  // a stray "*/" is plain text
}

TEST_CASE("consecutive lines carry their state; reloaded forgets it") {
    Fixture f("seq.py", "x = \"\"\"a\nb\nc\"\"\" def\ndef\n", py());
    CHECK(f.line_styles(0) == std::vector<Style>{Style::lsp_string});
    CHECK(f.line_styles(1) == std::vector<Style>{Style::lsp_string});
    CHECK(f.line_styles(2) == std::vector<Style>{Style::lsp_string, Style::lsp_keyword});
    CHECK(f.line_styles(3) == std::vector<Style>{Style::lsp_keyword});
    f.hl->reloaded();
    CHECK(f.line_styles(2) == std::vector<Style>{Style::lsp_string, Style::lsp_keyword});
}

TEST_CASE("past the back-scan bound the state is assumed plain") {
    std::string content = "/*\n";
    const std::string filler(1023, 'x');
    while (content.size() < SyntaxHighlighter::kMaxBackScan + 4096) content += filler + "\n";
    content += "int z;\n";
    const std::size_t last = static_cast<std::size_t>(std::count(content.begin(), content.end(), '\n')) - 1;
    Fixture f("far.c", content, c_like());
    // Asked for first, the far line cannot see the opening "/*" and is colored as code.
    CHECK(f.line_styles(last) == std::vector<Style>{Style::lsp_keyword});
}

namespace {

// A highlighter with fixed answers that counts the events it is sent.
struct Fixed final : Highlighter {
    std::vector<StyleSpan> spans;
    int befores = 0, afters = 0, reloads = 0, saves = 0;
    std::pair<std::uint64_t, std::uint64_t> range{0, 0};
    std::optional<Clock::time_point> deadline;
    std::string text;
    std::vector<StyleSpan> spans_for_line(std::uint64_t, std::string_view) override { return spans; }
    void before_change(const ChangeEvent&) override { ++befores; }
    void after_change(const ChangeEvent&) override { ++afters; }
    void reloaded() override { ++reloads; }
    void saved() override { ++saves; }
    void visible_range_changed(std::uint64_t a, std::uint64_t b) override { range = {a, b}; }
    std::optional<Clock::time_point> tick(Clock::time_point) override { return deadline; }
    std::string status() const override { return text; }
};

struct Layers {
    Fixed* base;
    Fixed* over;
    std::unique_ptr<LayeredHighlighter> h;
    Layers() {
        auto b = std::make_unique<Fixed>();
        auto o = std::make_unique<Fixed>();
        base = b.get();
        over = o.get();
        h = std::make_unique<LayeredHighlighter>(std::move(b), std::move(o));
    }
};

}  // namespace

TEST_CASE("layering: the overlay's spans win and cut the base's") {
    Layers l;
    l.base->spans = {{0, 10, Style::lsp_string, 0}, {12, 14, Style::lsp_keyword, 0}, {20, 30, Style::lsp_comment, 0}};
    l.over->spans = {{3, 5, Style::lsp_variable, 0}, {8, 13, Style::lsp_function, kModDeclaration}, {20, 30, Style::lsp_type, 0}};
    CHECK(l.h->spans_for_line(0, "") == std::vector<StyleSpan>{{0, 3, Style::lsp_string, 0},
                                                             {3, 5, Style::lsp_variable, 0},
                                                             {5, 8, Style::lsp_string, 0},
                                                             {8, 13, Style::lsp_function, kModDeclaration},
                                                             {13, 14, Style::lsp_keyword, 0},
                                                             {20, 30, Style::lsp_type, 0}});
}

TEST_CASE("layering: either layer alone passes through") {
    Layers l;
    l.base->spans = {{0, 4, Style::lsp_keyword, 0}, {5, 9, Style::lsp_string, 0}};
    CHECK(l.h->spans_for_line(0, "") == l.base->spans);
    l.base->spans.clear();
    l.over->spans = {{2, 3, Style::lsp_variable, 0}};
    CHECK(l.h->spans_for_line(0, "") == l.over->spans);
}

TEST_CASE("layering: events reach both layers; the earlier deadline; the overlay's status") {
    Layers l;
    l.h->before_change(ChangeEvent{});
    l.h->after_change(ChangeEvent{});
    l.h->reloaded();
    l.h->saved();
    l.h->visible_range_changed(5, 9);
    for (Fixed* f : {l.base, l.over}) {
        CHECK(f->befores == 1);
        CHECK(f->afters == 1);
        CHECK(f->reloads == 1);
        CHECK(f->saves == 1);
        CHECK(f->range == std::pair<std::uint64_t, std::uint64_t>{5, 9});
    }
    const auto now = Highlighter::Clock::now();
    CHECK_FALSE(l.h->tick(now));
    l.base->deadline = now + std::chrono::seconds(2);
    CHECK(l.h->tick(now) == l.base->deadline);
    l.over->deadline = now + std::chrono::seconds(1);
    CHECK(l.h->tick(now) == l.over->deadline);
    l.base->text = "base";
    l.over->text = "LSP: x";
    CHECK(l.h->status() == "LSP: x");
}

TEST_CASE("every built-in language has a syntax description that colors a sample line") {
    const auto loaded = LanguageConfig::load(std::nullopt);
    CHECK_FALSE(loaded.warning.has_value());
    struct Sample {
        const char* path;
        const char* line;
        std::vector<Style> want;  // in this order, at least
    };
    const std::vector<Sample> samples = {
        {"/x/a.cpp", R"(if (x) return 0x1F; // c "s")", {Style::lsp_keyword, Style::lsp_keyword, Style::lsp_number, Style::lsp_comment}},
        {"/x/a.h", R"(const char* s = u8"x"; /* c */)", {Style::lsp_keyword, Style::lsp_keyword, Style::lsp_string, Style::lsp_comment}},
        {"/x/a.rs", R"(fn f<'a>() -> &'a str { let c = 'x'; "s" } // c)", {Style::lsp_keyword, Style::lsp_keyword, Style::lsp_string, Style::lsp_string, Style::lsp_comment}},
        {"/x/a.py", R"(def f(): return f"x" or None # 1)", {Style::lsp_keyword, Style::lsp_keyword, Style::lsp_string, Style::lsp_keyword, Style::constant, Style::lsp_comment}},
        {"/x/a.ts", R"(const s = `t`; let n = null; // 1)", {Style::lsp_keyword, Style::lsp_string, Style::lsp_keyword, Style::constant, Style::lsp_comment}},
        {"/x/a.js", R"(function f() { return 'x'; })", {Style::lsp_keyword, Style::lsp_keyword, Style::lsp_string}},
        {"/x/a.go", R"(func f() { return "x" } // 1)", {Style::lsp_keyword, Style::lsp_keyword, Style::lsp_string, Style::lsp_comment}},
        {"/x/a.json", R"({"a": true, "b": 1.5e3, "c": null})", {Style::lsp_string, Style::constant, Style::lsp_string, Style::lsp_number, Style::lsp_string, Style::constant}},
        {"/x/a.toml", R"(a = "x" # 1)", {Style::lsp_string, Style::lsp_comment}},
        {"/x/a.yaml", R"(a: 'x' # 1)", {Style::lsp_string, Style::lsp_comment}},
        {"/x/a.yml", R"(on: true)", {Style::constant}},
        {"/x/a.sh", R"(if [ "$x" ]; then echo ${#x}; fi # 1)", {Style::lsp_keyword, Style::lsp_string, Style::lsp_keyword, Style::lsp_keyword, Style::lsp_comment}},
        {"/x/CMakeLists.txt", R"(IF(X) # 1 "s")", {Style::lsp_keyword, Style::lsp_comment}},
        {"/x/b.cmake", R"(set(X "y" ON))", {Style::lsp_keyword, Style::lsp_string, Style::constant}},
    };
    for (const Sample& s : samples) {
        CAPTURE(s.path);
        const LanguageServerSpec* spec = loaded.config.find_for_path(s.path);
        REQUIRE(spec != nullptr);
        REQUIRE(spec->syntax.has_value());
        std::vector<Style> got;
        for (const StyleSpan& span : SyntaxHighlighter::scan_line(*spec->syntax, {}, s.line).spans) got.push_back(span.style);
        CHECK(got == s.want);
    }
}

TEST_CASE("built-in multi-line forms: Python docstrings, C comments, JavaScript template strings") {
    const auto loaded = LanguageConfig::load(std::nullopt);
    const SyntaxSpec& py = *loaded.config.find_for_path("a.py")->syntax;
    CHECK(SyntaxHighlighter::scan_line(py, {}, R"(    """Docs)").end.string >= 0);
    CHECK(SyntaxHighlighter::scan_line(py, {}, R"(x = rb'''raw)").end.string >= 0);
    const SyntaxSpec& c = *loaded.config.find_for_path("a.c")->syntax;
    CHECK(SyntaxHighlighter::scan_line(c, {}, "/* open").end.block_comment >= 0);
    const SyntaxSpec& js = *loaded.config.find_for_path("a.js")->syntax;
    CHECK(SyntaxHighlighter::scan_line(js, {}, "const t = `a").end.string >= 0);
    CHECK(SyntaxHighlighter::scan_line(js, {}, "const s = 'a").end == SyntaxState{});  // a quote does not carry
}

TEST_CASE("charLiteral: a quote is a string only around one character or one escape") {
    SyntaxSpec r;
    StringRule ch{"'", "'", '\\', false, {}};
    ch.char_literal = true;
    r.strings = {ch};
    r.numbers = true;
    CHECK(styles("impl<'a, 'b> Foo<'a, 'b> {", r).empty());  // lifetimes, never strings
    CHECK(styles("let c = 'x'; let e = 'é'; let n = '\\n';", r) ==
          Items{{"'x'", Style::lsp_string}, {"'é'", Style::lsp_string}, {"'\\n'", Style::lsp_string}});
    CHECK(styles("'\\u{1F600}'", r) == Items{{"'\\u{1F600}'", Style::lsp_string}});
    CHECK(styles("'\\''", r) == Items{{"'\\''", Style::lsp_string}});
    CHECK(styles("x = 1'000'000;", r) == Items{{"1", Style::lsp_number}, {"000", Style::lsp_number}, {"000", Style::lsp_number}});
    CHECK(styles("''", r).empty());  // no character at all
}

TEST_CASE("the built-in Rust and C++ data do not mistake lifetimes and digit separators for strings") {
    const auto loaded = LanguageConfig::load(std::nullopt);
    const SyntaxSpec& rs = *loaded.config.find_for_path("a.rs")->syntax;
    for (const StyleSpan& s : SyntaxHighlighter::scan_line(rs, {}, "impl<'a, 'b> Foo<'a, 'b> {").spans) CHECK(s.style != Style::lsp_string);
    const SyntaxSpec& cpp = *loaded.config.find_for_path("a.cpp")->syntax;
    for (const StyleSpan& s : SyntaxHighlighter::scan_line(cpp, {}, "x = 1'000'000;").spans) CHECK(s.style != Style::lsp_string);
}
