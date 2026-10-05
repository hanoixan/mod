#include <chrono>
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "edit/document.hpp"
#include "edit/sidecar.hpp"
#include "syntax/markdown.hpp"
#include "util/event_queue.hpp"
#include "time_budget.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "markdown_test";
    fs::create_directories(dir);
    const fs::path p = dir / name;
    fs::remove(p);
    fs::remove(sidecar_path_for(p));
    return p;
}

std::string_view style_name(Style s) {
    switch (s) {
        case Style::md_heading1: return "h1";
        case Style::md_heading2: return "h2";
        case Style::md_heading3: return "h3";
        case Style::md_heading4: return "h4";
        case Style::md_heading5: return "h5";
        case Style::md_heading6: return "h6";
        case Style::md_emphasis: return "em";
        case Style::md_strong: return "strong";
        case Style::md_strike: return "strike";
        case Style::md_code: return "code";
        case Style::md_code_block: return "block";
        case Style::md_link_text: return "text";
        case Style::md_link_url: return "url";
        case Style::md_quote: return "quote";
        case Style::md_list_marker: return "list";
        case Style::md_markup: return "m";
        default: return "?";
    }
}

struct Fixture {
    EventQueue q{{}};
    std::string text;
    std::unique_ptr<Document> doc;
    std::unique_ptr<MarkdownHighlighter> hl;

    Fixture(const std::string& name, std::string content) : text(std::move(content)) {
        const fs::path p = scratch(name);
        std::ofstream(p, std::ios::binary) << text;
        auto d = Document::open(p, q);
        REQUIRE(d);
        doc = std::move(*d);
        hl = std::make_unique<MarkdownHighlighter>(*doc);
        doc->add_listener(hl.get());
    }
    ~Fixture() {
        if (doc) doc->remove_listener(hl.get());
    }

    // Offset of line `n` in the current text.
    std::uint64_t line_start(std::size_t n) const {
        std::uint64_t off = 0;
        for (std::size_t i = 0; i < n; ++i) off = text.find('\n', off) + 1;
        return off;
    }
    std::string line(std::size_t n) const {
        const std::uint64_t s = line_start(n);
        const std::size_t e = text.find('\n', s);
        return text.substr(s, e == std::string::npos ? std::string::npos : e - s);
    }

    std::vector<StyleSpan> spans(std::size_t n) {
        const std::uint64_t s = line_start(n);
        const std::string l = line(n);
        auto out = hl->spans_for_line(s, l);
        // Sorted, non-overlapping, never crossing the line.
        for (std::size_t i = 0; i < out.size(); ++i) {
            CHECK(out[i].start < out[i].end);
            CHECK(out[i].start >= s);
            CHECK(out[i].end <= s + l.size());
            if (i > 0) CHECK(out[i - 1].end <= out[i].start);
        }
        return out;
    }

    // "[text](style)" for each styled run of line `n`; unstyled text is left bare.
    std::string render(std::size_t n) {
        const std::uint64_t s = line_start(n);
        const std::string l = line(n);
        std::string out;
        std::uint64_t pos = s;
        for (const StyleSpan& sp : spans(n)) {
            out += l.substr(pos - s, sp.start - pos);
            out += "[" + l.substr(sp.start - s, sp.end - sp.start) + "](" + std::string(style_name(sp.style)) + ")";
            pos = sp.end;
        }
        out += l.substr(pos - s);
        return out;
    }

    void edit(std::uint64_t offset, std::uint64_t remove, std::string_view insert) {
        doc->apply(offset, remove, InsertContent(insert), EditKind::other, offset, offset + insert.size());
        text.replace(offset, remove, insert);
    }
};

std::string render_one(const std::string& line) {
    static int counter = 0;
    Fixture f("one" + std::to_string(counter++) + ".md", line + "\n");
    return f.render(0);
}

}  // namespace

TEST_CASE("ATX headings") {
    CHECK(render_one("# Title") == "[#](m)[ Title](h1)");
    CHECK(render_one("### Three ###") == "[###](m)[ Three ](h3)[###](m)");
    CHECK(render_one("###### Six") == "[######](m)[ Six](h6)");
    CHECK(render_one("####### seven") == "####### seven");
    CHECK(render_one("#no space") == "#no space");
    CHECK(render_one("   ## indented") == "   [##](m)[ indented](h2)");
    CHECK(render_one("    # code-indented") == "    # code-indented");
    CHECK(render_one("# with `code`") == "[#](m)[ with ](h1)[`](m)[code](code)[`](m)");
}

TEST_CASE("emphasis, strong and strike follow the flanking rules within a line") {
    CHECK(render_one("a *b* c") == "a [*](m)[b](em)[*](m) c");
    CHECK(render_one("a _b_ c") == "a [_](m)[b](em)[_](m) c");
    CHECK(render_one("**bold**") == "[**](m)[bold](strong)[**](m)");
    CHECK(render_one("__bold__") == "[__](m)[bold](strong)[__](m)");
    CHECK(render_one("***both***") == "[***](m)[both](strong)[***](m)");
    CHECK(render_one("*a **b** c*") == "[*](m)[a ](em)[**](m)[b](strong)[**](m)[ c](em)[*](m)");
    CHECK(render_one("~~gone~~") == "[~~](m)[gone](strike)[~~](m)");
    CHECK(render_one("a ~~~not~~~ b") == "a ~~~not~~~ b");  // strike runs are one or two tildes
    CHECK(render_one("snake_case_name") == "snake_case_name");  // intraword _ does not open
    CHECK(render_one("2 * 3 * 4") == "2 * 3 * 4");              // not flanking
    CHECK(render_one("*unclosed") == "*unclosed");
    CHECK(render_one("a*b*c") == "a[*](m)[b](em)[*](m)c");      // intraword * does
    CHECK(render_one("\\*not\\*") == "[\\](m)*not[\\](m)*");
}

TEST_CASE("inline code, links and autolinks") {
    CHECK(render_one("`a*b*`") == "[`](m)[a*b*](code)[`](m)");
    CHECK(render_one("``a ` b``") == "[``](m)[a ` b](code)[``](m)");
    CHECK(render_one("`open") == "`open");
    CHECK(render_one("[text](http://x.y)") == "[[](m)[text](text)[](](m)[http://x.y](url)[)](m)");
    CHECK(render_one("![alt](a.png)") == "[![](m)[alt](text)[](](m)[a.png](url)[)](m)");
    CHECK(render_one("[a *b*](u)") == "[[](m)[a ](text)[*](m)[b](em)[*](](m)[u](url)[)](m)");
    CHECK(render_one("[no link]") == "[no link]");
    CHECK(render_one("[x](a(b)c)") == "[[](m)[x](text)[](](m)[a(b)c](url)[)](m)");
    CHECK(render_one("<https://a.b/c>") == "[<](m)[https://a.b/c](url)[>](m)");
    CHECK(render_one("<me@x.org>") == "[<](m)[me@x.org](url)[>](m)");
    CHECK(render_one("<not a link>") == "<not a link>");
    CHECK(render_one("*[a](b)*") == "[*[](m)[a](text)[](](m)[b](url)[)*](m)"  /* adjacent markup is one span */);
}

TEST_CASE("block quotes, lists and thematic breaks") {
    CHECK(render_one("> quoted") == "[>](m) [quoted](quote)");
    CHECK(render_one(">no space") == "[>](m)[no space](quote)");
    CHECK(render_one("> > *deep*") == "[>](m) [>](m) [*](m)[deep](em)[*](m)");
    CHECK(render_one("> # Head") == "[>](m) [#](m)[ Head](h1)");
    CHECK(render_one("- item") == "[-](list) item");
    CHECK(render_one("  * item") == "  [*](list) item");
    CHECK(render_one("12. item") == "[12.](list) item");
    CHECK(render_one("3) item") == "[3)](list) item");
    CHECK(render_one("-not") == "-not");
    CHECK(render_one("***") == "[***](m)");
    CHECK(render_one("- - -") == "[- - -](m)");
    CHECK(render_one("___") == "[___](m)");
    CHECK(render_one("--") == "--");
}

TEST_CASE("fenced code blocks, a CR before the LF, and an unclosed fence") {
    Fixture f("fence.md", "```cpp\ncode *x*\n```\nafter *e*\n~~~~\n```\n~~~\n~~~~\ndone\r\n``` x `y`\n");
    CHECK(f.render(0) == "[```cpp](m)");
    CHECK(f.render(1) == "[code *x*](block)");
    CHECK(f.render(2) == "[```](m)");
    CHECK(f.render(3) == "after [*](m)[e](em)[*](m)");
    CHECK(f.render(4) == "[~~~~](m)");
    CHECK(f.render(5) == "[```](block)");  // a different fence character does not close
    CHECK(f.render(6) == "[~~~](block)");  // too short to close
    CHECK(f.render(7) == "[~~~~](m)");
    CHECK(f.render(8) == "done\r");        // no spans: the CR is not part of the text styled
    CHECK(f.render(9) == "``` x [`](m)[y](code)[`](m)");  // a backtick in the info string: not a fence
    CHECK(f.render(1) == "[code *x*](block)");            // out of order, through the checkpoint path

    Fixture g("unclosed.md", "text\n```\nstill code\n\n# not a heading\n");
    CHECK(g.render(4) == "[# not a heading](block)");
    CHECK(g.render(2) == "[still code](block)");

    Fixture h("crlf.md", "# T\r\n**b**\r\n");
    CHECK(h.render(0) == "[#](m)[ T](h1)\r");
    CHECK(h.render(1) == "[**](m)[b](strong)[**](m)\r");
}

TEST_CASE("fence state across checkpoints after edits") {
    std::string text;
    for (int i = 0; i < 1000; ++i) {
        if (i == 10 || i == 900) text += "```\n";
        else text += "line " + std::to_string(i) + "\n";
    }
    Fixture f("checkpoints.md", text);
    auto plain = [&](std::size_t n) { return f.line(n); };
    auto block = [&](std::size_t n) { return "[" + f.line(n) + "](block)"; };
    CHECK(f.render(950) == plain(950));        // after the closing fence
    CHECK(f.hl->checkpoints().size() >= 3);    // about every 256 lines
    CHECK(f.render(500) == block(500));
    CHECK(f.render(5) == plain(5));

    // Remove the opening fence: old line 500 is plain and the old closing fence opens
    // a block that runs to the end.
    const std::uint64_t open = f.line_start(10);
    f.edit(open, 4, "");
    for (const auto& [off, cp] : f.hl->checkpoints()) CHECK(off <= open);
    CHECK(f.line(499) == "line 500");
    CHECK(f.render(499) == plain(499));
    CHECK(f.line(899) == "```");
    CHECK(f.render(899) == "[```](m)");
    CHECK(f.render(950) == block(950));

    // A tilde fence at line 600 opens a block the backtick fence cannot close.
    const std::uint64_t at = f.line_start(600);
    f.edit(at, 0, "~~~\n");
    CHECK(f.render(700) == block(700));
    CHECK(f.render(500) == plain(500));
    CHECK(f.line(900) == "```");
    CHECK(f.render(900) == "[```](block)");
    CHECK(f.render(950) == block(950));

    // Make it a backtick fence: line 900 now closes it.
    f.edit(at, 4, "```\n");
    CHECK(f.render(700) == block(700));
    CHECK(f.render(900) == "[```](m)");
    CHECK(f.render(950) == plain(950));
}

TEST_CASE("the back-scan is bounded at 4 MiB without checkpoints") {
    std::string text = "```\n";
    const std::string row(99, 'x');
    while (text.size() < (6u << 20)) text += row + "\n";
    text += "tail\n";
    const std::size_t lines = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
    SUBCASE("a jump, then scrolling through in steps") {
        Fixture f("bounded.md", text);
        // Jumping straight to the end: the opening fence is more than 4 MiB back.
        CHECK(f.render(lines - 1) == "tail");
        bool guessed = false;
        for (const auto& [off, cp] : f.hl->checkpoints()) guessed = guessed || !cp.trusted;
        CHECK(guessed);
        CHECK(f.render(lines - 1) == "tail");  // still the guess
        // Scrolling through from the top replaces the guesses, so the state is then right.
        for (std::size_t n = 0; n < lines; n += 20000) f.render(n);
        CHECK(f.render(lines - 1) == "[tail](block)");
        for (const auto& [off, cp] : f.hl->checkpoints()) CHECK(cp.trusted);
    }
    SUBCASE("a jump, then scrolling through line by line") {
        Fixture f("bounded_lines.md", text);
        CHECK(f.render(lines - 1) == "tail");
        // Render every line in order, as scrolling does: the memo path records
        // trusted checkpoints.
        std::uint64_t start = 0;
        for (std::size_t n = 0; n + 1 < lines; ++n) {
            const std::size_t e = f.text.find('\n', start);
            f.hl->spans_for_line(start, std::string_view(f.text).substr(start, e - start));
            start = e + 1;
        }
        CHECK(f.render(1) == "[" + row + "](block)");
        CHECK(f.render(lines - 1) == "[tail](block)");
    }
}

TEST_CASE("the delimiter stack is capped at 1024") {
    std::string line;
    for (int i = 0; i < 1100; ++i) line += "*x ";
    line += "y*";
    Fixture f("cap.md", line + "\n");
    const auto spans = f.spans(0);
    for (const StyleSpan& s : spans) CHECK(s.style != Style::md_emphasis);  // the closer past the cap is literal

    std::string few;
    for (int i = 0; i < 10; ++i) few += "*x ";
    few += "y*";
    Fixture g("nocap.md", few + "\n");
    bool emphasis = false;
    for (const StyleSpan& s : g.spans(0)) emphasis = emphasis || s.style == Style::md_emphasis;
    CHECK(emphasis);  // the same shape under the cap matches

    std::string stars(50000, '*');
    Fixture h("stars.md", "a " + stars + " b\n");
    h.spans(0);  // completes; spans are checked for order
}

TEST_CASE("hostile Markdown: link-like runs style and scan in linear time") {
    std::string line;
    while (line.size() < 60000) line += "[](";
    std::string autolinks;
    while (autolinks.size() < 400000) autolinks += "<a:b> ";
    Fixture f("hostile.md", line + "\n" + autolinks + "\n");
    auto start = std::chrono::steady_clock::now();
    (void)f.spans(0);
    (void)f.spans(1);
    const MarkdownOutline o = scan_markdown(f.doc->text());
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < time_budget(5.0));
    CHECK(o.links.size() <= kMaxOutlineLinks);
}
