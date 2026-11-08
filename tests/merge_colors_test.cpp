#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "syntax/merge_colors.hpp"

using namespace mod;

namespace {

// The text each span covers, with its style, in order: B banner, Y and T yours' and theirs'
// insertions, M one both made (in each), y and t their deletions from the base, X a deletion by both: "B:<<<<<<< yours", "Y:x".
std::vector<std::string> marks(std::string_view text, std::uint64_t base = 0) {
    std::vector<std::string> out;
    for (const StyleSpan& s : merge_color_spans(text, base)) {
        char kind = '?';
        switch (s.style) {
            case Style::merge_banner: kind = 'B'; break;
            case Style::merge_your_insertion: kind = 'Y'; break;
            case Style::merge_their_insertion: kind = 'T'; break;
            case Style::merge_your_deletion: kind = 'y'; break;
            case Style::merge_their_deletion: kind = 't'; break;
            case Style::merge_both_deletion: kind = 'X'; break;
            case Style::merge_both_insertion: kind = 'M'; break;
            default: break;
        }
        out.push_back(std::string(1, kind) + ":" + std::string(text.substr(s.start - base, s.end - s.start)));
    }
    return out;
}

using V = std::vector<std::string>;

}  // namespace

TEST_CASE("merge colors: a block's banners and the characters each side changed") {
    const std::string t =
        "before\n"
        "<<<<<<< yours\n"
        "int x = 1;\n"
        "||||||| base\n"
        "int x = 0;\n"
        "=======\n"
        "long x = 0;\n"
        ">>>>>>> theirs\n"
        "after\n";
    CHECK(marks(t) == V{"B:<<<<<<< yours", "Y:1", "B:||||||| base", "t:i", "t:t", "y:0", "B:=======", "T:lo", "T:g", "B:>>>>>>> theirs"});
}

TEST_CASE("merge colors: offsets are the document's") {
    const std::string t = "<<<<<<<\na\n|||||||\nb\n=======\nb\n>>>>>>>\n";
    const auto spans = merge_color_spans(t, 1000);
    REQUIRE(!spans.empty());
    CHECK(spans.front().start == 1000);
    CHECK(spans.front().end == 1007);
    CHECK(marks(t, 1000) == V{"B:<<<<<<<", "Y:a", "B:|||||||", "y:b", "B:=======", "B:>>>>>>>"});
}

TEST_CASE("merge colors: git's labels, CRLF lines, whole lines added and removed, and a deletion by both") {
    const std::string t =
        "<<<<<<< HEAD\r\n"
        "keep\r\n"
        "new\r\n"
        "||||||| merged common ancestors\r\n"
        "keep\r\n"
        "old\r\n"
        "=======\r\n"
        "keep\r\n"
        ">>>>>>> feature\r\n";
    // Yours replaced "old" and theirs deleted the whole line: "old" is gone from both, its line end from theirs.
    CHECK(marks(t) == V{"B:<<<<<<< HEAD", "Y:new", "B:||||||| merged common ancestors", "X:old", "t:\r\n", "B:=======", "B:>>>>>>> feature"});
}

TEST_CASE("merge colors: identical sides have only their banners") {
    const std::string t = "<<<<<<< a\nsame\n||||||| b\nsame\n=======\nsame\n>>>>>>> c\n";
    CHECK(marks(t) == V{"B:<<<<<<< a", "B:||||||| b", "B:=======", "B:>>>>>>> c"});
}

TEST_CASE("merge colors: an empty section") {
    const std::string t = "<<<<<<<\n|||||||\nold\n=======\nold\n>>>>>>>";
    CHECK(marks(t) == V{"B:<<<<<<<", "B:|||||||", "y:old\n", "B:=======", "B:>>>>>>>"});
}

TEST_CASE("merge colors: a block that is not whole, or out of order, is plain text") {
    CHECK(marks("<<<<<<< a\nx\n||||||| b\ny\n=======\nz\n").empty());          // no end
    CHECK(marks("<<<<<<< a\nx\n=======\nz\n>>>>>>> c\n").empty());             // two-way, no base
    CHECK(marks("x\n||||||| b\ny\n=======\nz\n>>>>>>> c\n").empty());          // no start
    CHECK(marks("<<<<<<< a\nx\n=======\n||||||| b\ny\n>>>>>>> c\n").empty());  // out of order
    CHECK(marks("<<<<<<<< a\nx\n||||||| b\ny\n=======\nz\n>>>>>>> c\n").empty());  // eight, not seven
    CHECK(marks("<<<<<<<a\nx\n||||||| b\ny\n=======\nz\n>>>>>>> c\n").empty());    // no space after
    CHECK(marks(" <<<<<<< a\nx\n||||||| b\ny\n=======\nz\n>>>>>>> c\n").empty());  // not at the line's start
}

TEST_CASE("merge colors: a new start restarts the block; two blocks; text between them is plain") {
    const std::string t =
        "<<<<<<< stray\n"
        "<<<<<<< a\nx\n||||||| b\ny\n=======\ny\n>>>>>>> c\n"
        "between\n"
        "<<<<<<< a\np\n||||||| b\np\n=======\nq\n>>>>>>> c\n";
    CHECK(marks(t) == V{"B:<<<<<<< a", "Y:x", "B:||||||| b", "y:y", "B:=======", "B:>>>>>>> c",  //
                        "B:<<<<<<< a", "B:||||||| b", "t:p", "B:=======", "T:q", "B:>>>>>>> c"});
}

TEST_CASE("merge colors: characters are code points; the base's deletions are told apart by side") {
    const std::string t = "<<<<<<<\naéc\n|||||||\nabc\n=======\nabd\n>>>>>>>\n";
    CHECK(marks(t) == V{"B:<<<<<<<", "Y:é", "B:|||||||", "y:b", "t:c", "B:=======", "T:d", "B:>>>>>>>"});
    // Overlapping deletions: yours drops "bc", theirs "cd": b is yours', c both's, d theirs'.
    CHECK(marks("<<<<<<<\nade\n|||||||\nabcde\n=======\nabe\n>>>>>>>\n") == V{"B:<<<<<<<", "B:|||||||", "y:b", "X:c", "t:d", "B:=======", "B:>>>>>>>"});
}

TEST_CASE("merge colors: a section past the size limit has its differing middle colored whole") {
    std::string big(kMergeDiffMaxBytes + 100, 'a');
    std::string changed = big;
    changed[10] = 'b';
    changed[changed.size() - 10] = 'b';
    const std::string t = "<<<<<<<\n" + changed + "\n|||||||\n" + big + "\n=======\n" + big + "\n>>>>>>>\n";
    const auto spans = merge_color_spans(t, 0);
    REQUIRE(spans.size() == 6);
    CHECK(spans[1].style == Style::merge_your_insertion);
    CHECK(spans[1].end - spans[1].start == changed.size() - 19);  // from the first b to the last
    CHECK(spans[3].style == Style::merge_your_deletion);
    CHECK(spans[3].end - spans[3].start == big.size() - 19);
}

TEST_CASE("merge colors: an insertion both sides made is colored as both's, in each side") {
    // Both renamed n to count; yours also added a comment, theirs changed 0 to 1.
    const std::string t =
        "<<<<<<< yours\n"
        "int count = 0;  // total\n"
        "||||||| base\n"
        "int n = 0;\n"
        "=======\n"
        "int count = 1;\n"
        ">>>>>>> theirs\n";
    // count is cou + the base's n + t: the base's n is kept by both, so only cou and t are new.
    CHECK(marks(t) == V{"B:<<<<<<< yours", "M:cou", "M:t", "Y:  // total", "B:||||||| base", "t:0", "B:=======", "M:cou", "M:t", "T:1",
                        "B:>>>>>>> theirs"});
}

TEST_CASE("merge colors: the same insertion in different places is each side's own") {
    const std::string t = "<<<<<<<\nxab\n|||||||\nab\n=======\nabx\n>>>>>>>\n";
    CHECK(marks(t) == V{"B:<<<<<<<", "Y:x", "B:|||||||", "B:=======", "T:x", "B:>>>>>>>"});
    const std::string same = "<<<<<<<\naxb1\n|||||||\nab\n=======\naxb2\n>>>>>>>\n";
    CHECK(marks(same) == V{"B:<<<<<<<", "M:x", "Y:1", "B:|||||||", "B:=======", "M:x", "T:2", "B:>>>>>>>"});
}
