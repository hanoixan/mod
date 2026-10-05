#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "edit/document.hpp"
#include "edit/sidecar.hpp"
#include "edit/undo_tree.hpp"
#include "platform/terminal.hpp"
#include "ui/history_view.hpp"
#include "ui/screen.hpp"
#include "ui/theme.hpp"
#include "util/event_queue.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kNow = 1'700'000'000'000;

HistoryView::Clock fixed_clock() {
    return [] { return kNow; };
}

EditOp ins(std::uint64_t offset, std::string bytes) { return {offset, Payload::of_bytes({}), Payload::of_bytes(std::move(bytes))}; }

NodeMeta meta(EditKind kind, std::int64_t time = kNow) { return NodeMeta{0, 0, time, kind, 0, 0}; }

KeyEvent key(Key k) { return KeyEvent{k, 0, 0}; }

std::vector<std::string> texts(const HistoryView& v) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < v.rows().size(); ++i) out.push_back(v.row_text(i));
    return out;
}

// The layout sketch: 11 delete, 12 typed, 13 paste and 14 typed both children of 12; 14 current.
struct Sketch {
    UndoTree t;
    NodeId root = 0;
    Sketch() {
        for (int i = 0; i < 9; ++i) t.reserve_id();
        root = t.add_root(meta(EditKind::other), 0, {});  // 10
        t.commit({ins(0, "a")}, meta(EditKind::delete_));  // 11
        t.commit({ins(0, "b")}, meta(EditKind::typing));   // 12
        t.commit({ins(0, "c")}, meta(EditKind::paste));    // 13
        t.undo_step();
        t.commit({ins(0, "d")}, meta(EditKind::typing));  // 14
    }
};

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

std::string screen_row(const Screen& s, int row, int from, int to) {
    std::string out;
    for (int c = from; c < to; ++c) {
        const Cell& cell = s.cell(row, c);
        out.append(cell.utf8.data(), cell.len);
    }
    return out;
}

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "history_view_test";
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

template <class F>
bool pump(EventQueue& q, F done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done()) {
        if (q.drain() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (std::chrono::steady_clock::now() > deadline) return false;
    }
    return true;
}

bool wait_for_root_hash(EventQueue& q, const Document& d) {
    const NodeId root = d.history().root_of(*d.history().current());
    return pump(q, [&] { return d.history().root_base(root)->hash != ContentHash{}; });
}

std::string content(const Document& d) { return d.text().read(0, d.text().size()); }

// A clock the test moves by hand, for node times and coalescing.
struct Clock {
    std::shared_ptr<std::int64_t> now = std::make_shared<std::int64_t>(kNow);
    DocumentOptions options() const {
        DocumentOptions o;
        o.persist_history = PersistHistory::always;  // these tests are about the written history
        o.now_ms = [n = now] { return *n; };
        return o;
    }
    void advance(std::int64_t ms) const { *now += ms; }
};

void paste(Document& d, std::uint64_t at, std::string_view s) { d.apply(at, 0, s, EditKind::paste, at, at + s.size()); }

std::optional<std::size_t> row_of(const HistoryView& v, NodeId n) {
    for (std::size_t i = 0; i < v.rows().size(); ++i) {
        if (v.rows()[i].node == n) return i;
    }
    return std::nullopt;
}

// Moves the selection onto `n` with Down/Up keys only.
void select_node(HistoryView& v, NodeId n) {
    const std::size_t target = *row_of(v, n);
    while (v.selected() < target) v.handle_key(key(Key::Down));
    while (v.selected() > target) v.handle_key(key(Key::Up));
    REQUIRE(v.selected() == target);
}

}  // namespace

TEST_CASE("history_pane_width") {
    CHECK(history_pane_width(59).width == 57);
    CHECK(history_pane_width(59).full_width);
    CHECK(history_pane_width(60).width == 20);
    CHECK_FALSE(history_pane_width(60).full_width);
    CHECK(history_pane_width(80).width == 26);
    CHECK(history_pane_width(119).width == 39);
    CHECK(history_pane_width(120).width == 40);
    CHECK(history_pane_width(200).width == 40);
    CHECK(60 - history_pane_width(60).width - 3 == 37);
}

TEST_CASE("a single root") {
    UndoTree t;
    t.add_root(meta(EditKind::other), 0, {});
    HistoryView v(fixed_clock());
    v.open(t);
    REQUIRE(v.rows().size() == 1);
    CHECK(v.rows()[0].graph == "● ");
    CHECK(v.rows()[0].is_current);
    CHECK(v.row_text(0) == "● 1 other  just now");
    CHECK(v.selected() == 0);
}

TEST_CASE("the layout sketch: oldest at the top, the newest line down to the current state") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    // 13 is an older child of 12, so a side branch: closed, and 12 shows '>'.
    CHECK(texts(v) == std::vector<std::string>{"○ 10 other  just now", "○ 11 delete  just now", "○> 12 typed  just now",
                                               "● 14 typed  just now"});
    CHECK(v.rows()[3].is_current);
    CHECK(v.rows()[2].expandable);
    CHECK_FALSE(v.rows()[2].expanded);
    CHECK(v.selected() == 3);
}

TEST_CASE("relative times come from the injected clock") {
    UndoTree t;
    t.add_root(meta(EditKind::other, kNow - 3 * 86'400'000), 0, {});
    t.commit({ins(0, "a")}, meta(EditKind::typing, kNow - 2 * 3'600'000));
    t.commit({ins(0, "b")}, meta(EditKind::cut, kNow - 3 * 60'000));
    t.commit({ins(0, "c")}, meta(EditKind::replace_all, kNow - 5'000));
    HistoryView v(fixed_clock());
    v.open(t);
    CHECK(v.row_text(0) == "○ 1 other  3 days ago");
    CHECK(v.row_text(1) == "○ 2 typed  2 h ago");
    CHECK(v.row_text(2) == "○ 3 cut  3 min ago");
    CHECK(v.row_text(3) == "● 4 replace_all  just now");
}

namespace {

// 1 -> 2 -> 3 -> 4; 2 -> 5; 1 -> 6. Each newer child continues its parent's line.
UndoTree nested_tree() {
    UndoTree t;
    t.add_root(meta(EditKind::other), 0, {});  // 1
    t.commit({ins(0, "a")}, meta(EditKind::typing));  // 2
    t.commit({ins(0, "b")}, meta(EditKind::typing));  // 3
    t.commit({ins(0, "c")}, meta(EditKind::typing));  // 4
    t.undo_step();
    t.undo_step();
    t.commit({ins(0, "d")}, meta(EditKind::typing));  // 5, child of 2
    t.undo_step();
    t.undo_step();
    t.commit({ins(0, "e")}, meta(EditKind::typing));  // 6, child of 1
    return t;
}

std::vector<std::string> graphs(const HistoryView& v) {
    std::vector<std::string> out;
    for (const HistoryRow& r : v.rows()) out.push_back(r.graph);
    return out;
}

std::vector<std::optional<NodeId>> nodes(const HistoryView& v) {
    std::vector<std::optional<NodeId>> out;
    for (const HistoryRow& r : v.rows()) out.push_back(r.node);
    return out;
}

}  // namespace

TEST_CASE("branches open and close like folders: Right opens, Left closes or goes to where it split") {
    UndoTree t = nested_tree();
    HistoryView v(fixed_clock());
    v.open(t);
    CHECK(graphs(v) == std::vector<std::string>{"○> ", "● "});
    CHECK(nodes(v) == std::vector<std::optional<NodeId>>{1, 6});
    select_node(v, 1);
    v.handle_key(key(Key::Right));  // 2 hangs under 1, its line continuing with 5; 3 is closed
    CHECK(graphs(v) == std::vector<std::string>{"○ ", "├─○> ", "│ ○ ", "● "});
    CHECK(nodes(v) == std::vector<std::optional<NodeId>>{1, 2, 5, 6});
    CHECK(v.selected_node() == NodeId{1});  // the selection stays
    v.handle_key(key(Key::Right));  // on an open change: into its first branch
    CHECK(v.selected_node() == NodeId{2});
    v.handle_key(key(Key::Right));
    CHECK(graphs(v) == std::vector<std::string>{"○ ", "├─○ ", "│ ├─○ ", "│ │ ○ ", "│ ○ ", "● "});
    CHECK(nodes(v) == std::vector<std::optional<NodeId>>{1, 2, 3, 4, 5, 6});
    CHECK(v.rows()[3].depth == 2);
    select_node(v, 4);
    v.handle_key(key(Key::Left));  // not open: to the change its branch split from
    CHECK(v.selected_node() == NodeId{2});
    v.handle_key(key(Key::Left));  // open: closes
    CHECK(nodes(v) == std::vector<std::optional<NodeId>>{1, 2, 5, 6});
    CHECK(v.selected_node() == NodeId{2});
    v.handle_key(key(Key::Left));
    CHECK(v.selected_node() == NodeId{1});
    v.handle_key(key(Key::Left));
    CHECK(nodes(v) == std::vector<std::optional<NodeId>>{1, 6});
    v.handle_key(key(Key::Left));  // nothing more to close
    CHECK(nodes(v) == std::vector<std::optional<NodeId>>{1, 6});
}

TEST_CASE("on open, only the branches that lead to the current state are opened") {
    UndoTree t = nested_tree();
    t.set_position(4, {});  // make 4, deep in the branches, current
    REQUIRE(t.current() == NodeId{4});
    HistoryView v(fixed_clock());
    v.open(t);
    CHECK(graphs(v) == std::vector<std::string>{"○ ", "├─○ ", "│ ├─○ ", "│ │ ● ", "│ ○ ", "○ "});
    CHECK(v.selected_node() == NodeId{4});
    CHECK(v.rows()[v.selected()].is_current);
}

TEST_CASE("a linear chain of a million nodes is one line") {
    UndoTree t;
    t.add_root(meta(EditKind::other), 0, {});
    constexpr int kNodes = 1'000'000;
    for (int i = 1; i < kNodes; ++i) t.commit({ins(0, "x")}, meta(EditKind::typing));
    HistoryView v(fixed_clock());
    v.open(t);
    REQUIRE(v.rows().size() == static_cast<std::size_t>(kNodes));
    CHECK(v.rows().back().graph == "● ");
    bool one_line = true;
    for (std::size_t i = 0; i + 1 < v.rows().size(); ++i) one_line = one_line && v.rows()[i].graph == "○ ";
    CHECK(one_line);
    CHECK(v.rows().front().node == NodeId{1});
    CHECK(v.rows().back().node == NodeId{kNodes});
    CHECK(v.selected() == v.rows().size() - 1);  // the current state, at the bottom
    v.handle_key(key(Key::Home));
    CHECK(v.selected() == 0);
    v.close();
    CHECK(v.rows().empty());
}

TEST_CASE("current, save-point and redo-path markers") {
    Sketch s;
    s.t.mark_saved(12, 3, {});
    s.t.undo_step();  // 14 -> 12; 14 is the preferred child, so redo goes there
    HistoryView v(fixed_clock());
    v.open(s.t);
    const auto& rows = v.rows();
    CHECK(rows[*row_of(v, 12)].is_current);
    CHECK(rows[*row_of(v, 12)].is_save_point);
    CHECK(v.row_text(*row_of(v, 12)).starts_with("●> 12 typed saved"));
    CHECK(rows[*row_of(v, 14)].on_redo_path);
    CHECK_FALSE(rows[*row_of(v, 11)].on_redo_path);
    CHECK(rows[*row_of(v, 14)].graph == "○ ");
    CHECK(v.selected() == *row_of(v, 12));
    v.handle_key(key(Key::Right));
    CHECK_FALSE(v.rows()[*row_of(v, 13)].on_redo_path);
}

TEST_CASE("a forest: older trees above, one closed read-only row each, the current tree at the bottom") {
    UndoTree t;
    t.add_root(meta(EditKind::other), 0, {});  // the history before a reload
    const NodeId a = t.commit({ins(0, "a")}, meta(EditKind::typing));
    t.add_root(meta(EditKind::other), 0, {});  // after the reload
    const NodeId b = t.commit({ins(0, "b")}, meta(EditKind::typing));
    t.reset();  // Clear History retires everything so far
    const NodeId r3 = t.add_root(meta(EditKind::other), 0, {});
    const NodeId c = t.commit({ins(0, "c")}, meta(EditKind::typing));
    const NodeId r4 = t.add_root(meta(EditKind::other), 0, {});  // a reload after the clear
    const NodeId d = t.commit({ins(0, "d")}, meta(EditKind::typing));

    HistoryView v(fixed_clock());
    v.open(t);
    std::vector<bool> read_only;
    for (const HistoryRow& r : v.rows()) read_only.push_back(r.read_only);
    CHECK(nodes(v) == std::vector<std::optional<NodeId>>{a, b, c, r4, d});
    CHECK(read_only == std::vector<bool>{true, true, true, false, false});
    CHECK(v.rows()[2].graph == "○> ");  // an older tree, closed on its latest change

    select_node(v, c);
    const HistoryKeyResult refused = v.handle_key(key(Key::Enter));
    CHECK_FALSE(refused.jump.has_value());
    CHECK_FALSE(refused.closed);
    CHECK(refused.message == "read-only: history from before a reload or Clear History");
    CHECK(v.is_open());

    v.handle_key(key(Key::Right));  // opens that tree
    CHECK(nodes(v) == std::vector<std::optional<NodeId>>{a, b, r3, c, r4, d});
    CHECK(v.selected_node() == std::nullopt);  // still read-only
    CHECK(v.rows()[v.selected()].node == c);
    select_node(v, r3);
    v.handle_key(key(Key::Left));  // closes it again, onto its row
    CHECK(nodes(v) == std::vector<std::optional<NodeId>>{a, b, c, r4, d});
    CHECK(v.rows()[v.selected()].node == c);

    select_node(v, r4);
    const HistoryKeyResult ok = v.handle_key(key(Key::Enter));
    CHECK(ok.jump == r4);
    CHECK(ok.message.empty());
}

TEST_CASE("the selection moves row by row and clamps at both ends") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    CHECK(v.selected() == 3);
    v.handle_key(key(Key::Down));
    CHECK(v.selected() == 3);
    v.handle_key(key(Key::Up));
    CHECK(v.selected() == 2);
    v.handle_key(key(Key::Home));
    CHECK(v.selected() == 0);
    v.handle_key(key(Key::Up));
    CHECK(v.selected() == 0);
    v.handle_key(key(Key::End));
    CHECK(v.selected() == 3);
    v.handle_key(key(Key::PageUp));
    CHECK(v.selected() == 0);
    v.handle_key(key(Key::PageDown));
    CHECK(v.selected() == 3);
    // Typing is consumed and changes nothing.
    const HistoryKeyResult typed = v.handle_key(KeyEvent{Key::Char, U'x', 0});
    CHECK_FALSE(typed.jump.has_value());
    CHECK_FALSE(typed.closed);
    CHECK(v.selected() == 3);
}

TEST_CASE("Enter returns the node; Esc closes and returns nothing") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    v.handle_key(key(Key::Up));     // 12
    v.handle_key(key(Key::Right));  // opens 13's branch under it
    v.handle_key(key(Key::Down));
    const HistoryKeyResult r = v.handle_key(key(Key::Enter));
    CHECK(r.jump == NodeId{13});
    CHECK_FALSE(r.closed);
    CHECK(v.is_open());
    const HistoryKeyResult esc = v.handle_key(key(Key::Escape));
    CHECK_FALSE(esc.jump.has_value());
    CHECK(esc.closed);
    CHECK_FALSE(v.is_open());
    CHECK(s.t.current() == NodeId{14});
}

TEST_CASE("render truncates a long row to the area") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    v.handle_key(key(Key::Up));
    v.handle_key(key(Key::Right));
    NullTerminal term;
    Screen screen(term);
    screen.resize({10, 40});
    v.render(screen, Rect{1, 1, 5, 8});  // too short for the footer
    CHECK(screen_row(screen, 1, 1, 9) == "○ 10 oth");
    CHECK(screen_row(screen, 3, 1, 9) == "○ 12 typ");
    CHECK(screen_row(screen, 4, 1, 9) == "├─○ 13 p");
    CHECK(screen_row(screen, 5, 1, 9) == "● 14 typ");
    CHECK(screen_row(screen, 1, 9, 12) == "   ");  // nothing past the area
    CHECK(screen.cell(3, 1).attr == attr_for(Style::list_selected));
}

TEST_CASE("the footer: Clear History…, Trim History… and Persist History under a rule, with C, T and P underlined") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    NullTerminal term;
    Screen screen(term);
    screen.resize({12, 40});
    const Rect area{1, 1, 8, 30};  // rows 1..8: four list rows, the rule, three footer rows
    v.render(screen, area);
    std::string rule;
    for (int i = 0; i < 30; ++i) rule += "─";
    CHECK(screen_row(screen, 5, 1, 31) == rule);  // a single box-drawing line
    CHECK(screen_row(screen, 6, 1, 15) == "Clear History…");
    CHECK(screen_row(screen, 7, 1, 14) == "Trim History…");
    CHECK(screen_row(screen, 8, 1, 20) == "[ ] Persist History");
    CHECK((screen.cell(6, 1).attr.flags & kUnderline) != 0);  // the C
    CHECK((screen.cell(7, 1).attr.flags & kUnderline) != 0);  // the T
    CHECK((screen.cell(8, 5).attr.flags & kUnderline) != 0);  // the P of Persist
    CHECK((screen.cell(8, 1).attr.flags & kUnderline) == 0);
    CHECK((screen.cell(6, 2).attr.flags & kUnderline) == 0);
    CHECK(screen.cell(6, 1).attr.bg == attr_for(Style::menu).bg);
    CHECK(screen_row(screen, 1, 1, 9) == "○ 10 oth");  // the list keeps the top rows (the four fit)
    CHECK(screen_row(screen, 4, 1, 9) == "● 14 typ");
    CHECK(screen_row(screen, 9, 1, 31) == std::string(30, ' '));  // nothing past the area
}

TEST_CASE("the list scrolls within the rows above the footer") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    NullTerminal term;
    Screen screen(term);
    screen.resize({12, 40});
    const Rect area{0, 0, 7, 30};  // three list rows, then the footer
    v.render(screen, area);
    v.handle_key(key(Key::End));
    v.render(screen, area);
    const std::string last = v.row_text(v.selected());
    CHECK(screen_row(screen, 2, 0, 30).starts_with(last.substr(0, 10)));  // the last list row
    CHECK(screen.cell(2, 0).attr == attr_for(Style::list_selected));
    CHECK(screen_row(screen, 4, 0, 14) == "Clear History…");
}

TEST_CASE("the list has the folder tree's look: plain rows, the list highlight, unfocused while the text has the keys") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    v.handle_key(key(Key::Home));
    NullTerminal term;
    Screen screen(term);
    screen.resize({12, 40});
    const Rect area{0, 0, 7, 30};
    v.render(screen, area);
    CHECK(screen.cell(0, 29).attr == attr_for(Style::list_selected));
    CHECK(screen.cell(1, 29).attr == attr_for(Style::Default));  // plain, not the menu's band
    v.render(screen, area, false);  // Tab gave the keys to the previewed text
    CHECK(screen.cell(0, 29).attr == attr_for(Style::list_selected_unfocused));
}

TEST_CASE("the current state's ● is bold") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    v.handle_key(key(Key::Home));
    NullTerminal term;
    Screen screen(term);
    screen.resize({12, 40});
    v.render(screen, Rect{0, 0, 4, 30});
    CHECK(screen_row(screen, 3, 0, 1) == "●");
    CHECK((screen.cell(3, 0).attr.flags & kBold) != 0);
    CHECK((screen.cell(3, 2).attr.flags & kBold) == 0);
}

TEST_CASE("a pane under six rows has no footer") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    NullTerminal term;
    Screen screen(term);
    screen.resize({8, 40});
    v.render(screen, Rect{0, 0, 5, 30});
    for (int r = 0; r < 5; ++r) CHECK(screen_row(screen, r, 0, 30).find("History") == std::string::npos);
    CHECK(screen_row(screen, 3, 0, 8) == "● 14 typ");
}

TEST_CASE("C, T and P ask for Clear History, Trim History and Persist History; the panel stays open") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    for (const char32_t c : {U'c', U'C'}) {
        const HistoryKeyResult r = v.handle_key(KeyEvent{Key::Char, c, 0});
        REQUIRE(r.command.has_value());
        CHECK(*r.command == CommandId::ClearHistory);
        CHECK_FALSE(r.jump.has_value());
        CHECK_FALSE(r.closed);
    }
    for (const char32_t c : {U't', U'T'}) {
        const HistoryKeyResult r = v.handle_key(KeyEvent{Key::Char, c, 0});
        REQUIRE(r.command.has_value());
        CHECK(*r.command == CommandId::TrimHistory);
    }
    for (const char32_t c : {U'p', U'P'}) {
        const HistoryKeyResult r = v.handle_key(KeyEvent{Key::Char, c, 0});
        REQUIRE(r.command.has_value());
        CHECK(*r.command == CommandId::TogglePersistHistory);
    }
    CHECK(v.is_open());
    CHECK_FALSE(v.handle_key(KeyEvent{Key::Char, U'x', 0}).command.has_value());
    CHECK_FALSE(v.handle_key(KeyEvent{Key::Char, U'c', kAlt}).command.has_value());  // Alt+C is not C
    CHECK_FALSE(v.handle_key(key(Key::Enter)).command.has_value());
}

TEST_CASE("with Document.jump_to") {
    const fs::path p = scratch("jump.txt");
    write_file(p, "base\n");
    EventQueue q({});
    Clock clock;
    auto opened = Document::open(p, q, clock.options());
    REQUIRE(opened);
    Document& d = **opened;
    REQUIRE(wait_for_root_hash(q, d));
    paste(d, 0, "x");
    const NodeId nx = *d.history().current();
    clock.advance(2000);
    paste(d, 1, "y");
    const NodeId ny = *d.history().current();
    REQUIRE(d.undo());
    clock.advance(2000);
    paste(d, 1, "z");
    const NodeId nz = *d.history().current();
    CHECK(content(d) == "xzbase\n");

    HistoryView v(fixed_clock());
    v.open(d.history());
    select_node(v, nx);
    v.handle_key(key(Key::Right));  // y is an older branch of x: open it
    select_node(v, ny);
    const HistoryKeyResult r = v.handle_key(key(Key::Enter));
    REQUIRE(r.jump == ny);
    auto cursor = d.jump_to(*r.jump);
    REQUIRE(cursor);
    v.close();
    const std::string jumped = content(d);

    // The same as the undo and redo sequence through the common ancestor.
    REQUIRE(d.jump_to(nz));
    REQUIRE(d.undo());
    CHECK(*d.history().current() == nx);
    REQUIRE(d.cycle_branch(1));
    while (d.history().node_info(nx).preferred_child != ny) REQUIRE(d.cycle_branch(1));
    REQUIRE(d.redo());
    CHECK(content(d) == jumped);
    CHECK(jumped == "xybase\n");

    // The next edit after a jump is a new child of the chosen node.
    REQUIRE(d.jump_to(nx));
    clock.advance(2000);
    paste(d, 0, "!");
    const NodeId child = *d.history().current();
    CHECK(d.history().meta(child).parent == nx);
    CHECK(d.history().node_info(nx).children.size() == 3);
    v.open(d.history());
    CHECK(v.rows().back().node == child);
    CHECK(v.rows().back().is_current);
}

TEST_CASE("a jump to a node that predates verification is refused while verifying") {
    const fs::path p = scratch("verify.txt");
    write_file(p, "one\n");
    EventQueue q({});
    Clock clock;
    NodeId old_node = 0;
    {
        auto doc = Document::open(p, q, clock.options());
        REQUIRE(doc);
        REQUIRE(wait_for_root_hash(q, **doc));
        paste(**doc, 0, "X");
        clock.advance(2000);
        paste(**doc, 0, "Y");
        old_node = *(*doc)->history().current();
        REQUIRE((*doc)->save());
    }
    auto doc = Document::open(p, q, clock.options());
    REQUIRE(doc);
    Document& d = **doc;
    REQUIRE(d.history_state() == HistoryState::verifying);  // the queue is not drained yet
    HistoryView v(fixed_clock());
    v.open(d.history());
    const NodeId older = d.history().meta(old_node).parent;
    select_node(v, older);
    const HistoryKeyResult r = v.handle_key(key(Key::Enter));
    REQUIRE(r.jump == older);
    const auto jumped = d.jump_to(*r.jump);
    REQUIRE_FALSE(jumped);
    CHECK(content(d) == "YXone\n");
    CHECK(v.is_open());
}

TEST_CASE("after a prune: one tree rooted at the anchor, nothing dimmed for what was removed") {
    const fs::path p = scratch("prune.txt");
    write_file(p, "base\n");
    EventQueue q({});
    Clock clock;
    auto opened = Document::open(p, q, clock.options());
    REQUIRE(opened);
    Document& d = **opened;
    REQUIRE(wait_for_root_hash(q, d));
    // An earlier history, retired by Clear History.
    paste(d, 0, "old ");
    REQUIRE(d.save());
    REQUIRE(d.clear_history());
    REQUIRE(wait_for_root_hash(q, d));
    const std::vector<NodeId> retired = d.history().retired_roots();
    REQUIRE_FALSE(retired.empty());
    // a and b are old; ten days later c, then d on another branch from b... e from c.
    clock.advance(1000);
    paste(d, 0, "a");
    const NodeId a = *d.history().current();
    clock.advance(1000);
    paste(d, 0, "b");
    const NodeId b = *d.history().current();
    clock.advance(10 * kDayMs);
    paste(d, 0, "c");
    const NodeId c = *d.history().current();
    REQUIRE(d.undo());
    clock.advance(1000);
    paste(d, 0, "d");
    const NodeId dd = *d.history().current();
    REQUIRE(d.save());

    REQUIRE(d.prune_history(*clock.now - 5 * kDayMs));
    CHECK_FALSE(d.history().contains(a));
    HistoryView v(fixed_clock());
    v.open(d.history());
    std::vector<std::optional<NodeId>> live;
    for (const HistoryRow& r : v.rows()) {
        if (!r.read_only) live.push_back(r.node);
    }
    // One live tree under the anchor b: its newest line b, d, with c's branch closed.
    CHECK(live == std::vector<std::optional<NodeId>>{b, dd});
    CHECK(d.history().roots() == std::vector<NodeId>{b});
    // The cleared history's rows are still there, dimmed.
    bool saw_retired = false;
    for (const HistoryRow& r : v.rows()) {
        if (d.history().is_retired(r.node)) {
            saw_retired = true;
            CHECK(r.read_only);
        }
    }
    CHECK(saw_retired);

    // Enter on the other kept branch jumps there through the new root.
    select_node(v, b);
    v.handle_key(key(Key::Right));
    select_node(v, c);
    const HistoryKeyResult r = v.handle_key(key(Key::Enter));
    REQUIRE(r.jump == c);
    REQUIRE(d.jump_to(c));
    CHECK(content(d) == "cbaold base\n");
}

TEST_CASE("the Persist History checkbox shows the state it is given") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    NullTerminal term;
    Screen screen(term);
    screen.resize({12, 40});
    v.set_persist(true);
    v.render(screen, Rect{0, 0, 8, 30});
    CHECK(screen_row(screen, 7, 0, 19) == "[x] Persist History");
    v.set_persist(false);
    v.render(screen, Rect{0, 0, 8, 30});
    CHECK(screen_row(screen, 7, 0, 19) == "[ ] Persist History");
}

TEST_CASE("selected_node follows the selection") {
    Sketch s;
    HistoryView v(fixed_clock());
    v.open(s.t);
    CHECK(v.selected_node() == NodeId{14});
    v.handle_key(key(Key::Up));
    CHECK(v.selected_node() == NodeId{12});
}

TEST_CASE("Left and Right on an empty history read nothing") {
    UndoTree tree;
    HistoryView view;
    view.open(tree);
    for (Key k : {Key::Left, Key::Right}) CHECK_FALSE(view.handle_key(KeyEvent{k}).jump.has_value());
}

TEST_CASE("an empty history takes every key without reading past its rows") {
    UndoTree tree;
    HistoryView view;
    view.open(tree);
    for (Key k : {Key::Up, Key::Down, Key::PageUp, Key::PageDown, Key::Home, Key::End, Key::Enter}) {
        const HistoryKeyResult r = view.handle_key(KeyEvent{k});
        CHECK_FALSE(r.jump.has_value());
    }
    CHECK(view.handle_key(KeyEvent{Key::Char, U'c'}).command == CommandId::ClearHistory);
    CHECK(view.handle_key(KeyEvent{Key::Escape}).closed);
    CHECK_FALSE(view.is_open());
}
