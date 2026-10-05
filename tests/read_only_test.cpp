#include <doctest/doctest.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "app/read_only.hpp"
#include "syntax/markdown.hpp"
#include "text/piece_tree.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

struct Text {
    PieceTree tree;
    MarkdownOutline outline;
    explicit Text(std::string_view s) {
        tree.insert(0, std::as_bytes(std::span(s.data(), s.size())));
        outline = scan_markdown(tree);
    }
};

// Offsets:    0         1         2         3         4
//             0123456789012345678901234567890123456789012345
const std::string_view kDoc = "[a](a.md) x [b](sub/b.md#keys) y [c](#local)\n# Local\n";

}  // namespace

TEST_CASE("Tab and Shift+Tab step through the links and wrap at both ends") {
    const Text t(kDoc);
    auto start = [&](std::uint64_t pos, int dir) { return ReadOnlyNav::next_link(t.outline, pos, dir)->start; };
    CHECK(start(0, 1) == 12);    // a link starting at the cursor is not "next"
    CHECK(start(5, 1) == 12);
    CHECK(start(12, 1) == 33);
    CHECK(start(33, 1) == 0);    // wraps to the first
    CHECK(start(33, -1) == 12);
    CHECK(start(12, -1) == 0);
    CHECK(start(0, -1) == 33);   // wraps to the last
    CHECK(start(40, -1) == 33);  // from inside a link, Shift+Tab goes to that link's start
    const Text none("no links here\n");
    CHECK_FALSE(ReadOnlyNav::next_link(none.outline, 0, 1).has_value());
}

TEST_CASE("the link under the cursor") {
    const Text t(kDoc);
    CHECK(ReadOnlyNav::link_at(t.outline, 0)->target == "a.md");
    CHECK(ReadOnlyNav::link_at(t.outline, 8)->target == "a.md");
    CHECK_FALSE(ReadOnlyNav::link_at(t.outline, 9).has_value());  // the end is outside
    CHECK(ReadOnlyNav::link_at(t.outline, 20)->target == "sub/b.md#keys");
}

TEST_CASE("resolving: a relative file, a file with an anchor, a local anchor, a web address") {
    const Text t(kDoc);
    const fs::path shown = "/docs/guide.md";
    LinkAction a = ReadOnlyNav::resolve(*ReadOnlyNav::link_at(t.outline, 0), shown, t.outline);
    CHECK(a.kind == LinkAction::open);
    CHECK(a.path == fs::path("/docs/a.md"));
    CHECK(a.anchor.empty());
    a = ReadOnlyNav::resolve(*ReadOnlyNav::link_at(t.outline, 12), shown, t.outline);
    CHECK(a.kind == LinkAction::open);
    CHECK(a.path == fs::path("/docs/sub/b.md"));
    CHECK(a.anchor == "keys");
    a = ReadOnlyNav::resolve(*ReadOnlyNav::link_at(t.outline, 33), shown, t.outline);
    CHECK(a.kind == LinkAction::jump);
    CHECK(a.offset == 45);  // the "# Local" heading
    const MarkdownLink web{0, 1, 0, 1, "https://example.com"};
    a = ReadOnlyNav::resolve(web, shown, t.outline);
    CHECK(a.kind == LinkAction::message);
    CHECK(a.text == "https://example.com (web addresses are not opened)");
    const MarkdownLink mail{0, 1, 0, 1, "mailto:me@example.com"};
    CHECK(ReadOnlyNav::resolve(mail, shown, t.outline).kind == LinkAction::message);
    const MarkdownLink missing_anchor{0, 1, 0, 1, "#nowhere"};
    a = ReadOnlyNav::resolve(missing_anchor, shown, t.outline);
    CHECK(a.kind == LinkAction::message);
    CHECK(a.text == "no heading #nowhere in guide.md");
    const MarkdownLink up{0, 1, 0, 1, "../other/x.md"};
    CHECK(ReadOnlyNav::resolve(up, shown, t.outline).path == fs::path("/other/x.md"));
    const MarkdownLink spaced{0, 1, 0, 1, "my%20file.md"};
    CHECK(ReadOnlyNav::resolve(spaced, shown, t.outline).path == fs::path("/docs/my file.md"));  // percent-escapes decoded
}

TEST_CASE("the trail: visit, back and forward keep each entry's position; a new visit drops the forward entries") {
    ReadOnlyNav nav;
    nav.reset({"/d/base.md", 0, 0});
    CHECK(nav.at_base());
    CHECK_FALSE(nav.back({"/d/base.md", 3, 0}).has_value());
    nav.visit({"/d/base.md", 10, 2}, {"/d/a.md", 0, 0});
    nav.visit({"/d/a.md", 7, 1}, {"/d/b.md", 0, 0});
    CHECK(nav.size() == 3);
    CHECK(nav.here().path == fs::path("/d/b.md"));
    auto e = nav.back({"/d/b.md", 4, 0});
    REQUIRE(e.has_value());
    CHECK(e->path == fs::path("/d/a.md"));
    CHECK(e->cursor == 7);  // where it was left
    e = nav.back({"/d/a.md", 7, 1});
    REQUIRE(e.has_value());
    CHECK(e->cursor == 10);
    CHECK(nav.at_base());
    e = nav.forward({"/d/base.md", 11, 2});
    REQUIRE(e.has_value());
    CHECK(e->path == fs::path("/d/a.md"));
    e = nav.forward({"/d/a.md", 7, 1});
    CHECK(e->cursor == 4);  // b.md as it was left
    CHECK_FALSE(nav.forward({"/d/b.md", 4, 0}).has_value());
    nav.back({"/d/b.md", 4, 0});
    nav.visit({"/d/a.md", 7, 1}, {"/d/c.md", 0, 0});  // drops b.md
    CHECK(nav.size() == 3);
    CHECK_FALSE(nav.forward({"/d/c.md", 0, 0}).has_value());
    CHECK(nav.back({"/d/c.md", 0, 0})->path == fs::path("/d/a.md"));
    CHECK(nav.back({"/d/a.md", 0, 0})->cursor == 11);  // the base kept its newer position
}
