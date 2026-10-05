#include <doctest/doctest.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "syntax/markdown.hpp"
#include "text/piece_tree.hpp"

using namespace mod;

namespace {

struct Text {
    PieceTree tree;
    explicit Text(std::string_view s) { tree.insert(0, std::as_bytes(std::span(s.data(), s.size()))); }
};

std::vector<std::string> targets(const MarkdownOutline& o) {
    std::vector<std::string> out;
    for (const MarkdownLink& l : o.links) out.push_back(l.target);
    return out;
}

std::string text_of(const Text& t, std::uint64_t a, std::uint64_t b) { return t.tree.read(a, b - a); }

}  // namespace

TEST_CASE("inline links: the whole link, its visible text and its destination") {
    const Text t("See [the guide](guide.md) and [keys](keys.md#ctrl-k \"Keys\").\n");
    const MarkdownOutline o = scan_markdown(t.tree);
    REQUIRE(o.links.size() == 2);
    CHECK(text_of(t, o.links[0].start, o.links[0].end) == "[the guide](guide.md)");
    CHECK(text_of(t, o.links[0].text_start, o.links[0].text_end) == "the guide");
    CHECK(o.links[0].target == "guide.md");
    CHECK(o.links[1].target == "keys.md#ctrl-k");  // the title is not part of the destination
    CHECK_FALSE(o.truncated);
}

TEST_CASE("destinations: angle brackets, escapes, nested parentheses, images") {
    const Text t("[a](<my file.md>) [b](x\\)y.md) [c](f(1).md) ![pic](p.png)\n");
    const MarkdownOutline o = scan_markdown(t.tree);
    CHECK(targets(o) == std::vector<std::string>{"my file.md", "x)y.md", "f(1).md", "p.png"});
}

TEST_CASE("autolinks, and links that are not links") {
    const Text t("<https://example.com/a> `[code](x.md)` \\[not](y.md) [open\n");
    const MarkdownOutline o = scan_markdown(t.tree);
    REQUIRE(o.links.size() == 1);
    CHECK(o.links[0].target == "https://example.com/a");
    CHECK(text_of(t, o.links[0].text_start, o.links[0].text_end) == "https://example.com/a");
}

TEST_CASE("reference links resolve against definitions anywhere in the document") {
    const Text t("[Full][guide] and [guide][] and [guide] and [missing][nope].\n\n[GUIDE]: ./guide.md \"Title\"\n");
    const MarkdownOutline o = scan_markdown(t.tree);
    CHECK(targets(o) == std::vector<std::string>{"./guide.md", "./guide.md", "./guide.md"});
}

TEST_CASE("nothing inside a fenced code block is a link or a heading") {
    const Text t("# Top\n```\n[x](x.md)\n# not a heading\n```\n[y](y.md)\n");
    const MarkdownOutline o = scan_markdown(t.tree);
    CHECK(targets(o) == std::vector<std::string>{"y.md"});
    REQUIRE(o.headings.size() == 1);
    CHECK(o.headings[0].slug == "top");
}

TEST_CASE("headings and their GitHub-style slugs, duplicates numbered") {
    const Text t("# Getting Started\n## Keys & Clipboard: Ctrl+K!\nSetext Title\n============\n## Getting started\n#NotHeading\n");
    const MarkdownOutline o = scan_markdown(t.tree);
    REQUIRE(o.headings.size() == 4);
    CHECK(o.headings[0].slug == "getting-started");
    CHECK(o.headings[0].start == 0);
    CHECK(o.headings[1].slug == "keys--clipboard-ctrlk");
    CHECK(o.headings[2].slug == "setext-title");
    CHECK(o.headings[3].slug == "getting-started-1");
    CHECK(heading_slug("Hello  World") == "hello--world");
    CHECK(heading_slug("Under_score-dash") == "under_score-dash");
    CHECK(heading_slug("Café Ünïcode") == "café-ünïcode");  // letters beyond ASCII are kept
}

TEST_CASE("links are in document order with absolute offsets across lines") {
    const Text t("line one\n[a](a.md)\r\nx [b](b.md)\n");
    const MarkdownOutline o = scan_markdown(t.tree);
    REQUIRE(o.links.size() == 2);
    CHECK(o.links[0].start == 9);
    CHECK(o.links[1].start == 22);
}

TEST_CASE("a document longer than the cap is scanned only up to it") {
    std::string s;
    for (int i = 0; i < 100; ++i) s += "[l](l.md)\n";  // 10 bytes per line
    const Text t(s);
    const MarkdownOutline o = scan_markdown(t.tree, 95);
    CHECK(o.truncated);
    CHECK(o.links.size() == 9);  // whole lines within the first 95 bytes
}
