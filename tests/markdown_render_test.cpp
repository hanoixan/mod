#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "syntax/markdown_render.hpp"
#include "time_budget.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> texts(const RenderedPage& p) {
    std::vector<std::string> out;
    for (const RenderedLine& l : p.lines) out.push_back(l.text);
    return out;
}

// The style of the run that covers byte `at` of line `n`, or Default.
Style style_at(const RenderedPage& p, std::size_t n, std::size_t at) {
    for (const RenderSpan& s : p.lines[n].spans)
        if (at >= s.begin && at < s.end) return s.style;
    return Style::Default;
}

// The text of each link, gathered from its pieces.
std::vector<std::string> link_texts(const RenderedPage& p) {
    std::vector<std::string> out;
    for (const RenderedLink& l : p.links) {
        std::string t;
        for (const LinkPiece& piece : l.pieces) {
            if (!t.empty()) t += ' ';
            t += p.lines[piece.line].text.substr(piece.begin, piece.end - piece.begin);
        }
        out.push_back(t);
    }
    return out;
}

int columns(const std::string& s) {
    int n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

// The source offsets of line `n`'s bytes, kGenerated as -1, as text for a readable check.
std::string map_of(const RenderedPage& p, std::size_t n) {
    std::string out;
    for (std::uint64_t o : p.lines[n].source) out += (o == kGenerated ? std::string("-") : std::to_string(o)) + " ";
    return out;
}

// The source bytes the shown bytes of line `n` stand for, which must equal them.
void check_shown_bytes(std::string_view src, const RenderedPage& p, std::size_t n) {
    const RenderedLine& l = p.lines[n];
    REQUIRE(l.source.size() == l.text.size());
    for (std::size_t b = 0; b < l.text.size(); ++b) {
        if (l.source[b] == kGenerated) continue;
        REQUIRE(l.source[b] < src.size());
        const char want = src[l.source[b]];
        // A space between words stands for any whitespace, a line break included.
        if (l.text[b] == ' ') {
            CHECK((want == ' ' || want == '\n' || want == '\t'));
        } else {
            CHECK(l.text[b] == want);
        }
    }
}

}  // namespace

TEST_CASE("headings: no marks, their style, underlines for levels 1 and 2, anchors") {
    const RenderedPage p = render_markdown("# Getting started\n\nText.\n\n## The screen\n\n### Keys\n", 40);
    const auto t = texts(p);
    REQUIRE(t.size() >= 7);
    CHECK(t[0] == "Getting started");
    CHECK(t[1] == "═══════════════");
    CHECK(style_at(p, 0, 0) == Style::md_heading1);
    CHECK(t[2] == "");
    CHECK(t[3] == "Text.");
    CHECK(std::find(t.begin(), t.end(), "The screen") != t.end());
    CHECK(std::find(t.begin(), t.end(), "──────────") != t.end());
    CHECK(std::find(t.begin(), t.end(), "Keys") != t.end());
    REQUIRE(p.anchors.size() == 3);
    CHECK(p.anchors[0].first == "getting-started");
    CHECK(p.anchors[0].second == 0);
    CHECK(p.anchors[1].first == "the-screen");
    CHECK(t[p.anchors[1].second] == "The screen");
    CHECK(p.lines[p.anchors[2].second].source_line == 7);
}

TEST_CASE("paragraphs wrap at the width with words kept whole; a long word is broken") {
    const RenderedPage p = render_markdown("one two three four five six seven\neight nine\n", 20);
    const auto t = texts(p);
    for (const std::string& line : t) CHECK(columns(line) <= 20);
    CHECK(t[0] == "one two three four");
    CHECK(t[1] == "five six seven eight");
    CHECK(t[2] == "nine");
    const RenderedPage q = render_markdown(std::string(45, 'x') + "\n", 20);
    CHECK(texts(q)[0] == std::string(20, 'x'));
    CHECK(texts(q)[2] == std::string(5, 'x'));
}

TEST_CASE("inline marks are dropped and their styles set; escapes show the character") {
    const RenderedPage p = render_markdown("a **bold** b *em* c _em2_ d ~~gone~~ e `code` f \\*star\\*\n", 80);
    const std::string line = p.lines[0].text;
    CHECK(line == "a bold b em c em2 d gone e code f *star*");
    CHECK(style_at(p, 0, line.find("bold")) == Style::md_strong);
    CHECK(style_at(p, 0, line.find("em ")) == Style::md_emphasis);
    CHECK(style_at(p, 0, line.find("em2")) == Style::md_emphasis);
    CHECK(style_at(p, 0, line.find("gone")) == Style::md_strike);
    CHECK(style_at(p, 0, line.find("code")) == Style::md_code);
    CHECK(style_at(p, 0, line.find("*star")) == Style::Default);
    CHECK(style_at(p, 0, 0) == Style::Default);
}

TEST_CASE("links show their text only, record their targets, and keep pieces across a wrap") {
    const RenderedPage p = render_markdown("Please see [the settings page](settings.md#keys) and [web](https://example.com).\n", 20);
    std::string all;
    for (const auto& l : texts(p)) all += l + "\n";
    CHECK(all.find("](") == std::string::npos);
    CHECK(all.find("settings.md") == std::string::npos);
    CHECK(all.find("example.com") == std::string::npos);
    REQUIRE(p.links.size() == 2);
    CHECK(p.links[0].target == "settings.md#keys");
    CHECK(p.links[1].target == "https://example.com");
    CHECK(link_texts(p)[0] == "the settings page");
    CHECK(p.links[0].pieces.size() == 2);  // "Please see the" / "settings page and"
    const LinkPiece& first = p.links[0].pieces[0];
    CHECK(style_at(p, first.line, first.begin) == Style::md_link_text);
}

TEST_CASE("lists: bullets, numbers, hanging indents and nesting") {
    const RenderedPage p = render_markdown("- one\n- a longer item that wraps around\n  - nested\n\n1. first\n2. second\n", 22);
    const auto t = texts(p);
    CHECK(t[0] == "• one");
    CHECK(t[1] == "• a longer item that");
    CHECK(t[2] == "  wraps around");
    CHECK(t[3] == "  • nested");
    CHECK(std::find(t.begin(), t.end(), "1. first") != t.end());
    CHECK(std::find(t.begin(), t.end(), "2. second") != t.end());
}

TEST_CASE("quotes, fenced code and rules") {
    const RenderedPage p = render_markdown("> quoted text\n\n```bash\nuv tool install x  # long line kept whole\n```\n\n---\n", 20);
    const auto t = texts(p);
    CHECK(t[0] == "│ quoted text");
    CHECK(style_at(p, 0, t[0].find("quoted")) == Style::md_quote);
    const auto code = std::find(t.begin(), t.end(), "    uv tool install x  # long line kept whole");
    REQUIRE(code != t.end());
    CHECK(style_at(p, static_cast<std::size_t>(code - t.begin()), 4) == Style::md_code_block);
    for (const auto& line : t) CHECK(line.find("```") == std::string::npos);
    CHECK(t.back() == std::string("────────────────────"));
}

TEST_CASE("tables: aligned columns, a header rule, inline marks not counted") {
    const RenderedPage p = render_markdown("| Key | Command |\n|---|---|\n| `Ctrl+S` | Save |\n| `F1` | [Help](help.md) |\n", 60);
    const auto t = texts(p);
    REQUIRE(t.size() >= 4);
    CHECK(t[0] == "Key    │ Command");
    CHECK(t[1] == "───────┼────────");
    CHECK(t[2] == "Ctrl+S │ Save");
    CHECK(t[3] == "F1     │ Help");
    REQUIRE(p.links.size() == 1);
    CHECK(p.links[0].target == "help.md");
    CHECK(style_at(p, 2, 0) == Style::md_code);
}

TEST_CASE("source lines are recorded for every rendered line") {
    const RenderedPage p = render_markdown("# A\n\npara one\npara two\n\n- item\n", 40);
    for (const RenderedLine& l : p.lines) CHECK(l.source_line >= 1);
    CHECK(p.lines[0].source_line == 1);
    const auto t = texts(p);
    const auto item = std::find(t.begin(), t.end(), "• item");
    REQUIRE(item != t.end());
    CHECK(p.lines[static_cast<std::size_t>(item - t.begin())].source_line == 6);
}

TEST_CASE("no page of the real manual renders a Markdown mark") {
    for (const auto& entry : fs::recursive_directory_iterator(fs::path(MOD_SOURCE_DIR) / "docs" / "manual")) {
        if (entry.path().extension() != ".md") continue;
        CAPTURE(entry.path().filename().string());
        std::ifstream in(entry.path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const RenderedPage p = render_markdown(text, 80);
        for (std::size_t n = 0; n < p.lines.size(); ++n) {
            const RenderedLine& l = p.lines[n];
            CAPTURE(l.text);
            CHECK(l.text.find("](") == std::string::npos);
            CHECK_FALSE(l.text.starts_with("#"));
            CHECK(l.text.find("```") == std::string::npos);
            // Marks may show only where they are the text of a code span.
            if (const std::size_t at = l.text.find("**"); at != std::string::npos) CHECK(style_at(p, n, at) == Style::md_code);
        }
    }
}

TEST_CASE("source map: marks are hidden, shown bytes map to their source") {
    const std::string src = "Some **bold** and `code`, \\*not em\\*.\n";
    const RenderedPage p = render_markdown(src, 80);
    REQUIRE(p.lines.size() >= 1);
    CHECK(p.lines[0].text == "Some bold and code, *not em*.");
    check_shown_bytes(src, p, 0);
    CHECK(p.lines[0].source[5] == 7);    // "b" of bold, after "**"
    CHECK(p.lines[0].source[14] == 19);  // "c" of code, after the backtick
    CHECK(p.lines[0].source[20] == 27);  // the escaped "*" maps to itself, not its backslash
}

TEST_CASE("source map: a heading, a link, a list, a quote") {
    const std::string src = "## Title\n\nSee [the guide](guide.md) now.\n\n- one\n- two\n\n> said\n";
    const RenderedPage p = render_markdown(src, 80);
    const auto t = texts(p);
    REQUIRE(t.size() >= 9);
    CHECK(t[0] == "Title");
    CHECK(p.lines[0].source[0] == 3);
    CHECK(map_of(p, 1).starts_with("- - -"));  // the underline is made up
    for (std::size_t n = 0; n < p.lines.size(); ++n) check_shown_bytes(src, p, n);
    const auto see = std::ranges::find(t, std::string("See the guide now."));
    REQUIRE(see != t.end());
    const auto n = static_cast<std::size_t>(see - t.begin());
    CHECK(p.lines[n].source[4] == 15);  // "t" of "the guide", after "["
    CHECK(p.lines[n].source[13] == 35);  // the space after the link stands for the source's
    const auto one = std::ranges::find(t, std::string("• one"));
    REQUIRE(one != t.end());
    const auto k = static_cast<std::size_t>(one - t.begin());
    CHECK(p.lines[k].source[0] == kGenerated);  // the bullet
    CHECK(p.lines[k].source[std::string("• ").size()] == src.find("one"));
    const auto q = std::ranges::find(t, std::string("│ said"));
    REQUIRE(q != t.end());
    const auto& ql = p.lines[static_cast<std::size_t>(q - t.begin())];
    CHECK(ql.source[0] == kGenerated);
    CHECK(ql.source[std::string("│ ").size()] == src.find("said"));
}

TEST_CASE("source map: a wrapped paragraph joins its lines, a fence keeps its own, a table maps its cells") {
    const std::string src = "alpha beta\ngamma delta\n\n```\n  code\n```\n\n| a | b |\n|---|---|\n| x | y |\n";
    const RenderedPage p = render_markdown(src, 20);
    const auto t = texts(p);
    CHECK(t[0] == "alpha beta gamma");
    CHECK(p.lines[0].source[10] == 10);  // the line break between "beta" and "gamma"
    for (std::size_t n = 0; n < p.lines.size(); ++n) check_shown_bytes(src, p, n);
    const auto code = std::ranges::find(t, std::string("      code"));
    REQUIRE(code != t.end());
    const auto& cl = p.lines[static_cast<std::size_t>(code - t.begin())];
    CHECK(cl.source[0] == kGenerated);  // the four-column indent
    CHECK(cl.source[4] == src.find("  code"));
    const auto row = std::ranges::find(t, std::string("x │ y"));
    REQUIRE(row != t.end());
    const auto& rl = p.lines[static_cast<std::size_t>(row - t.begin())];
    CHECK(rl.source[0] == src.find("x"));
    CHECK(rl.source[1] == kGenerated);
    CHECK(rl.source[rl.text.size() - 1] == src.find("y"));
}

TEST_CASE("hostile Markdown: deep nesting and unmatched marks lay out quickly, without exhausting the stack") {
    const auto repeat = [](std::string_view s, int n) {
        std::string out;
        for (int i = 0; i < n; ++i) out += s;
        return out;
    };
    const std::vector<std::pair<const char*, std::string>> inputs = {
        {"nested links", repeat("[", 100000) + "x" + repeat("](a)", 100000)},
        {"open brackets", repeat("[", 200000)},
        {"links without a closing paren", repeat("[a](", 60000)},
        {"nested emphasis", repeat("*_", 100000) + "x" + repeat("_*", 100000)},
        {"nested strong", repeat("**", 100000) + "x" + repeat("**", 100000)},
        {"backtick runs", "a" + repeat("`", 3000) + repeat("``x", 30000)},  // not at the line start: no fence
    };
    for (const auto& [name, text] : inputs) {
        const std::string input_name = name;
        CAPTURE(input_name);
        const auto start = std::chrono::steady_clock::now();
        const RenderedPage page = render_markdown(text, 80);
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        CHECK(!page.lines.empty());
        CHECK(seconds < time_budget(5.0));
    }
}
