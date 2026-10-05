#include <doctest/doctest.h>

#include <optional>
#include <string>

#include "edit/editor.hpp"
#include "ui/reading_layout.hpp"

using namespace mod;

namespace {

const std::string kSrc = "# Head\n\nSome **bold** text here.\n\n- one\n- two\n";
//                        0123456 7 8901234567890123456789012 3 4567890123 456789

}  // namespace

TEST_CASE("locate: a shown byte where it is drawn, a hidden mark at the next shown byte") {
    const ReadingLayout r(kSrc, 40);
    // Lines: "Head", "════", "", "Some bold text here.", "", "• one", "• two"
    REQUIRE(r.page().lines.size() == 7);
    CHECK(r.locate(2).line == 0);
    CHECK(r.locate(2).col == 0);
    CHECK(r.locate(0).line == 0);  // "# " is hidden: the next shown byte
    CHECK(r.locate(0).col == 0);
    const auto bold = r.locate(15);  // "b" after "**"
    CHECK(bold.line == 3);
    CHECK(bold.col == 5);
    CHECK(r.locate(13).col == 5);  // the "**" itself
    CHECK(r.locate(kSrc.size()).line == 6);  // past the end: the last position
}

TEST_CASE("move: Left and Right skip hidden marks, Up and Down pass over lines with no positions") {
    const ReadingLayout r(kSrc, 40);
    std::optional<int> sticky;
    CHECK(r.move(Motion::Right, 12, 10, sticky) == 15);  // "Some " then "bold": "**" skipped
    CHECK(r.move(Motion::Left, 15, 10, sticky) == 12);
    CHECK(r.move(Motion::Right, 18, 10, sticky) == 21);  // the closing "**" is skipped too
    // From "Head" down: the underline and the blank line have no positions.
    const std::uint64_t down = r.move(Motion::Down, 3, 10, sticky);  // "e" of Head, column 1
    CHECK(r.locate(down).line == 3);
    CHECK(r.locate(down).col == 1);
    CHECK(sticky == 1);
    const std::uint64_t back = r.move(Motion::Up, down, 10, sticky);
    CHECK(back == 3);
    CHECK(r.move(Motion::LineStart, 20, 10, sticky) == 8);
    CHECK_FALSE(sticky);
    CHECK(r.locate(r.move(Motion::LineEnd, 8, 10, sticky)).col == 20);  // after "here."
    CHECK(r.move(Motion::DocStart, 30, 10, sticky) == 2);
    CHECK(r.locate(r.move(Motion::DocEnd, 2, 10, sticky)).line == 6);
}

TEST_CASE("move: Up and Down keep the column across shorter lines") {
    const ReadingLayout r("a long first line\n\nab\n\nanother long line\n", 40);
    std::optional<int> sticky;
    const std::uint64_t mid = r.move(Motion::Down, 10, 10, sticky);  // column 10 of the first line
    CHECK(r.locate(mid).line == 2);
    CHECK(r.locate(mid).col == 2);  // the end of "ab"
    const std::uint64_t last = r.move(Motion::Down, mid, 10, sticky);
    CHECK(r.locate(last).line == 4);
    CHECK(r.locate(last).col == 10);
}

TEST_CASE("visible_text: the text as shown, a block's wrapped lines joined by a space") {
    const ReadingLayout r(kSrc, 40);
    CHECK(r.visible_text(8, 33) == "Some bold text here.");
    CHECK(r.visible_text(8, 19) == "Some bold");
    CHECK(r.visible_text(34, kSrc.size()) == "• one\n• two");
    const ReadingLayout narrow("alpha beta gamma delta epsilon zeta eta theta iota\n", 20);
    REQUIRE(narrow.page().lines.size() >= 3);
    CHECK(narrow.visible_text(0, 51) == "alpha beta gamma delta epsilon zeta eta theta iota");
}
