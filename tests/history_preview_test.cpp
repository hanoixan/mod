#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "app/history_preview.hpp"
#include "edit/sidecar.hpp"
#include "util/event_queue.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

// Thirty numbered lines, then one edit at the start: the history is the root and that edit.
struct Fixture {
    EventQueue q{{}};
    Clipboard clipboard;
    DocumentSlot slot;
    std::string original;

    Fixture() {
        for (int i = 0; i < 30; ++i) original += "line " + std::to_string(i) + "\n";
        const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "history_preview_test";
        fs::create_directories(dir);
        const fs::path p = dir / "lines.txt";
        fs::remove(sidecar_path_for(p));
        std::ofstream(p, std::ios::binary | std::ios::trunc) << original;
        auto d = Document::open(p, q);
        REQUIRE(d);
        slot.doc = std::move(*d);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!slot.doc->text().line_count()) {
            if (q.drain() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            REQUIRE(std::chrono::steady_clock::now() < deadline);
        }
        slot.editor = std::make_unique<Editor>(*slot.doc, clipboard, 4);
        slot.view = std::make_unique<EditorView>(*slot.doc, *slot.editor, nullptr, 4);
        slot.editor->insert_text("X");
        REQUIRE(slot.doc->history().current() != root());
    }
    std::string text() const { return slot.doc->text().read(0, slot.doc->text().size()); }
    NodeId root() const { return slot.doc->history().root_of(*slot.doc->history().current()); }
    std::uint64_t line_start(int line) { return *slot.doc->line_start(static_cast<std::uint64_t>(line), true); }
};

}  // namespace

TEST_CASE("the preview shows a node's text read-only at the same lines, and ending puts everything back") {
    Fixture f;
    f.slot.view->set_top(f.line_start(10));
    HistoryPreview preview;
    REQUIRE(preview.begin(f.slot, f.root(), 10, 40));
    CHECK(f.slot.doc->previewing());
    CHECK(f.slot.view->previewing());
    CHECK(f.text() == f.original);
    CHECK(f.slot.view->top() == f.line_start(10));  // the same line, though one byte earlier

    REQUIRE(preview.show(f.slot, std::nullopt, 10, 40));  // a row without a node: the current state
    CHECK(f.text() == "X" + f.original);

    preview.end(f.slot);
    CHECK_FALSE(f.slot.doc->previewing());
    CHECK_FALSE(f.slot.view->previewing());
    CHECK(f.text() == "X" + f.original);
    CHECK(f.slot.view->top() == f.line_start(10));
}

TEST_CASE("Tab gives the previewed text the focus, whose motions scroll it by lines and pages") {
    Fixture f;
    HistoryPreview preview;
    REQUIRE(preview.begin(f.slot, f.root(), 10, 40));
    CHECK_FALSE(preview.text_focused());
    preview.toggle_focus();
    CHECK(preview.text_focused());
    CHECK(preview.scroll(f.slot, KeyEvent{Key::Down}, 5));
    CHECK(f.slot.view->top() == f.line_start(1));
    CHECK(preview.scroll(f.slot, KeyEvent{Key::PageDown}, 5));
    CHECK(f.slot.view->top() == f.line_start(6));
    CHECK(preview.scroll(f.slot, KeyEvent{Key::Up}, 5));
    CHECK(f.slot.view->top() == f.line_start(5));
    CHECK(preview.scroll(f.slot, KeyEvent{Key::End}, 5));
    CHECK(f.slot.view->top() == f.line_start(25));  // the last page
    CHECK(preview.scroll(f.slot, KeyEvent{Key::Home}, 5));
    CHECK(f.slot.view->top() == 0);
    CHECK_FALSE(preview.scroll(f.slot, KeyEvent{Key::Char, U'a'}, 5));
    preview.end(f.slot);
    CHECK_FALSE(preview.text_focused());
}

TEST_CASE("the preview shows where the selected step changed the text, two rows clear of the edges") {
    Fixture f;  // its own edit is at the start of the text
    f.slot.editor->select_range(f.line_start(25), f.line_start(25));
    f.slot.editor->insert_text("Y");  // a later step, far below the top
    f.slot.view->set_top(0);
    HistoryPreview preview;
    REQUIRE(preview.begin(f.slot, std::nullopt, 10, 40));  // the current step: the Y
    CHECK(f.slot.view->top() == f.line_start(18));          // line 25 two rows above the bottom
    REQUIRE(preview.show(f.slot, f.slot.doc->history().node_info(*f.slot.doc->history().current()).meta.parent, 10, 40));
    CHECK(f.slot.view->top() == f.line_start(0));           // the X, back at the very top
    preview.end(f.slot);
    CHECK(f.slot.view->top() == 0);                         // where it was
}
