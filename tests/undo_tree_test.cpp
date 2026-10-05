#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "edit/document.hpp"
#include "edit/sidecar.hpp"
#include "edit/undo_tree.hpp"
#include "util/event_queue.hpp"
#include "util/hash.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "undo_tree_test";
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

// Replaces `p` as another program's atomic save would: a new file renamed over it. A
// document that still maps the old file keeps a valid mapping, so its background scan
// can never read past the end of a truncated file.
void replace_file(const fs::path& p, std::string_view bytes) {
    fs::path tmp = p;
    tmp += ".new";
    write_file(tmp, bytes);
    fs::rename(tmp, p);
}

ContentHash hash_of(std::string_view s) {
    ContentHasher h;
    h.update(std::as_bytes(std::span(s.data(), s.size())));
    return h.finish();
}

EditOp ins(std::uint64_t offset, std::string bytes) { return {offset, Payload::of_bytes({}), Payload::of_bytes(std::move(bytes))}; }
EditOp del(std::uint64_t offset, std::string bytes) { return {offset, Payload::of_bytes(std::move(bytes)), Payload::of_bytes({})}; }

template <class F>
bool pump(EventQueue& q, F done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done()) {
        if (q.drain() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (std::chrono::steady_clock::now() > deadline) return false;
    }
    return true;
}

// For a document opened without history: waits until the scan has given its root a hash.
bool wait_for_root_hash(EventQueue& q, const Document& d) {
    const NodeId root = d.history().root_of(*d.history().current());
    return pump(q, [&] { return d.history().root_base(root)->hash != ContentHash{}; });
}

std::string read_back(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string content(const Document& d) { return d.text().read(0, d.text().size()); }

// History written to the sidecar from the start, as before Persist History was off by default.
DocumentOptions persisted() {
    DocumentOptions o;
    o.persist_history = PersistHistory::always;
    return o;
}

// A clock the test moves by hand.
struct Clock {
    std::shared_ptr<std::int64_t> now = std::make_shared<std::int64_t>(1'000'000);
    DocumentOptions options() const {
        DocumentOptions o;
        o.now_ms = [n = now] { return *n; };
        o.persist_history = PersistHistory::always;  // these tests are about the written history
        return o;
    }
    void advance(std::int64_t ms) const { *now += ms; }
};

void type(Document& d, std::uint64_t at, std::string_view s) { d.apply(at, 0, s, EditKind::typing, at, at + s.size()); }

}  // namespace

TEST_CASE("branching: an edit after undo creates a sibling and redo follows the preferred child") {
    UndoTree t;
    const NodeId root = t.add_root({}, 0, {});
    CHECK(t.current() == root);
    const NodeId a = t.commit({ins(0, "a")}, {});
    const NodeId b = t.commit({ins(1, "b")}, {});
    CHECK(t.meta(b).parent == a);
    CHECK(t.undo_step() == b);
    CHECK(t.current() == a);
    const NodeId c = t.commit({ins(1, "c")}, {});
    CHECK(t.node_info(a).children.size() == 2);
    CHECK(t.node_info(a).preferred_child == c);
    CHECK(t.undo_step() == c);
    CHECK(t.redo_step() == c);
    CHECK(t.redo_step() == std::nullopt);  // a leaf
    CHECK(t.undo_step() == c);
    CHECK(t.undo_step() == a);
    CHECK(t.undo_step() == std::nullopt);  // at the root
    CHECK(t.redo_step() == a);
    CHECK(t.redo_step() == c);

    SUBCASE("undo makes redo go back where you were") {
        t.undo_step();
        t.undo_step();
        t.redo_step();
        CHECK(t.node_info(a).preferred_child == c);
    }
    SUBCASE("cycle_branch walks siblings in id order") {
        t.undo_step();
        const auto first = t.cycle_branch(+1);
        REQUIRE(first);
        CHECK(first->index == 1);
        CHECK(first->count == 2);
        CHECK(t.node_info(a).preferred_child == b);
        CHECK(t.cycle_branch(+1)->index == 2);
        CHECK(t.cycle_branch(-1)->index == 1);
        CHECK(t.redo_step() == b);
        CHECK_FALSE(t.cycle_branch(+1));  // fewer than 2 children
    }
}

TEST_CASE("path goes through the lowest common ancestor, and follow sets preferred children") {
    UndoTree t;
    const NodeId root = t.add_root({}, 0, {});
    const NodeId a = t.commit({ins(0, "a")}, {});
    const NodeId b = t.commit({ins(1, "b")}, {});
    t.undo_step();
    const NodeId c = t.commit({ins(1, "c")}, {});
    const NodeId d = t.commit({ins(2, "d")}, {});

    const auto p = t.path(b, d);
    REQUIRE(p.size() == 3);
    CHECK(p[0].node == b);
    CHECK(p[0].direction == StepDirection::undo);
    CHECK(p[1].node == c);
    CHECK(p[1].direction == StepDirection::redo);
    CHECK(p[2].node == d);
    CHECK(t.path(d, d).empty());
    const auto up = t.path(d, root);
    REQUIRE(up.size() == 3);
    CHECK(up[2].node == a);

    // From d to b, along the non-preferred child of a.
    const auto steps = t.path(d, b);
    std::vector<PreferredChange> changes;
    for (const PathStep& s : steps) changes.push_back(t.follow(s));
    CHECK(t.current() == b);
    CHECK(t.node_info(a).preferred_child == b);
    REQUIRE(changes.size() == 3);
    CHECK(changes.back().parent == a);
    CHECK(changes.back().child == b);
}

TEST_CASE("reset retires the forest; ids are not reused and stale rebinds are ignored") {
    UndoTree t;
    const NodeId root = t.add_root({}, 3, hash_of("abc"));
    PieceTree text;
    const std::string big(5000, 'x');
    const PieceRun run = text.store(std::as_bytes(std::span(big.data(), big.size())));
    const NodeId a = t.commit({EditOp{0, Payload::of_bytes({}), Payload::of_run(run, 5000)}},
                              NodeMeta{0, 0, 42, EditKind::paste, 0, 5000});
    t.mark_saved(a, 5003, {});
    t.reset();
    CHECK_FALSE(t.current());
    CHECK(t.roots().empty());
    REQUIRE(t.retired_roots().size() == 1);
    CHECK(t.retired_roots()[0] == root);
    CHECK(t.is_retired(a));
    const NodeInfo info = t.node_info(a);
    CHECK(info.meta.kind == EditKind::paste);
    CHECK(info.meta.time_unix_ms == 42);
    CHECK(info.meta.cursor_after == 5000);
    CHECK(info.op_count == 0);
    CHECK_FALSE(info.preferred_child);
    CHECK(info.is_save_point);
    CHECK(t.node_info(root).children.size() == 1);
    CHECK(t.ops(a).empty());
    t.rebind_payload(a, 0, Which::inserted, SidecarRef{100, 5000});  // a stale closure: ignored

    const NodeId fresh = t.add_root({}, 3, hash_of("abc"));
    CHECK(fresh > a);
    CHECK(t.roots() == std::vector<NodeId>{fresh});
    CHECK(t.path(fresh, a).empty());
    const NodeId n = t.commit({ins(0, "z")}, {});
    CHECK(n > fresh);
}

TEST_CASE("amend_current merges a typing burst and a run of deletions") {
    UndoTree t;
    t.add_root({}, 0, {});
    t.commit({ins(0, "a")}, {});
    t.amend_current(ins(1, "b"), 2);
    t.amend_current(ins(2, "c"), 3);
    REQUIRE(t.ops(*t.current()).size() == 1);
    CHECK(std::get<Payload::Inline>(t.ops(*t.current())[0].inserted.form).bytes == "abc");
    CHECK(t.meta(*t.current()).cursor_after == 3);

    t.commit({del(2, "c")}, {});
    t.amend_current(del(1, "b"), 1);  // Backspace
    t.amend_current(del(1, "x"), 1);  // Delete
    const auto ops = t.ops(*t.current());
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].offset == 1);
    CHECK(std::get<Payload::Inline>(ops[0].removed.form).bytes == "bcx");

    t.amend_current(ins(7, "q"), 8);  // not adjacent: a second op
    CHECK(t.ops(*t.current()).size() == 2);
}

TEST_CASE("is_at_saved tracks undo back to the save point") {
    UndoTree t;
    const NodeId root = t.add_root({}, 0, {});
    t.mark_saved(root, 0, {});
    CHECK(t.is_at_saved());
    t.commit({ins(0, "a")}, {});
    CHECK_FALSE(t.is_at_saved());
    t.undo_step();
    CHECK(t.is_at_saved());
}

TEST_CASE("load_node rejects unknown parents and ids not above their parent") {
    UndoTree t;
    REQUIRE(t.load_root(NodeMeta{5, kNoParent, 0, EditKind::other, 0, 0}, 0, {}));
    CHECK(t.load_node(NodeMeta{7, 5, 0, EditKind::typing, 0, 1}, {ins(0, "a")}));
    CHECK_FALSE(t.load_node(NodeMeta{9, 8, 0, EditKind::typing, 0, 1}, {}));  // unknown parent
    CHECK_FALSE(t.load_node(NodeMeta{6, 7, 0, EditKind::typing, 0, 1}, {}));  // id below its parent
    CHECK_FALSE(t.load_node(NodeMeta{7, 5, 0, EditKind::typing, 0, 1}, {}));  // duplicate
    CHECK(t.next_id() == 8);
    CHECK(t.node_info(5).preferred_child == NodeId{7});
}

// ---- Document-level history ---------------------------------------------------------------

TEST_CASE("coalescing timeout: 0.999 s coalesces, 1.000 s starts a new node") {
    const fs::path p = scratch("coalesce.txt");
    write_file(p, "");
    EventQueue q({});
    Clock clock;
    auto doc = Document::open(p, q, clock.options());
    REQUIRE(doc);
    Document& d = **doc;
    type(d, 0, "a");
    clock.advance(999);
    type(d, 1, "b");
    const NodeId first = *d.history().current();
    clock.advance(1000);
    type(d, 2, "c");
    CHECK(*d.history().current() != first);
    CHECK(d.history().ops(first).size() == 1);
    CHECK(content(d) == "abc");
    REQUIRE(d.undo());
    CHECK(content(d) == "ab");
    REQUIRE(d.undo());
    CHECK(content(d) == "");
}

TEST_CASE("Document coalescing rules") {
    const fs::path p = scratch("rules.txt");
    write_file(p, "");
    EventQueue q({});
    Clock clock;
    auto doc = Document::open(p, q, clock.options());
    REQUIRE(doc);
    Document& d = **doc;

    SUBCASE("whitespace after a word starts a new node: undo is word by word") {
        type(d, 0, "h");
        type(d, 1, "i");
        type(d, 2, " ");
        type(d, 3, "y");
        type(d, 4, "o");
        CHECK(content(d) == "hi yo");
        REQUIRE(d.undo());
        CHECK(content(d) == "hi");
        REQUIRE(d.undo());
        CHECK(content(d) == "");
    }
    SUBCASE("typing elsewhere starts a new node") {
        type(d, 0, "ab");
        type(d, 0, "X");
        REQUIRE(d.undo());
        CHECK(content(d) == "ab");
    }
    SUBCASE("deletions coalesce; other kinds always close the node") {
        type(d, 0, "abcd");
        clock.advance(2000);
        d.apply(3, 1, std::string_view{}, EditKind::delete_, 4, 3);
        d.apply(2, 1, std::string_view{}, EditKind::delete_, 3, 2);
        CHECK(content(d) == "ab");
        d.apply(2, 0, std::string_view("P"), EditKind::paste, 2, 3);
        d.apply(3, 0, std::string_view("Q"), EditKind::paste, 3, 4);
        CHECK(content(d) == "abPQ");
        CHECK(*d.undo() == 3);
        CHECK(content(d) == "abP");
        REQUIRE(d.undo());
        CHECK(content(d) == "ab");
        CHECK(*d.undo() == 4);  // the deletions, as one node
        CHECK(content(d) == "abcd");
    }
    SUBCASE("an explicit group is one node; an empty group is dropped") {
        const NodeId before = *d.history().current();
        {
            Document::GroupGuard g(d, EditKind::replace_all);
        }
        CHECK(*d.history().current() == before);
        {
            Document::GroupGuard g(d, EditKind::replace_all);
            type(d, 0, "x");
            type(d, 1, " y");
            d.apply(0, 1, std::string_view("z"), EditKind::replace, 0, 1);
        }
        CHECK(content(d) == "z y");
        CHECK(d.history().meta(*d.history().current()).kind == EditKind::replace_all);
        REQUIRE(d.undo());
        CHECK(content(d) == "");
    }
}

TEST_CASE("history is kept in <file>.history, and an old <file>.history is never read") {
    const fs::path p = scratch("renamed.txt");
    fs::remove(p.parent_path() / "renamed.txt.history");
    fs::remove(p.parent_path() / "renamed.txt.mod");
    EventQueue q({});
    write_file(p, "one\n");
    {
        auto doc = Document::open(p, q, persisted());
        REQUIRE(doc);
        REQUIRE(wait_for_root_hash(q, **doc));
        type(**doc, 0, "X");
        REQUIRE((*doc)->save());
    }
    const fs::path side = p.parent_path() / "renamed.txt.history";
    CHECK(fs::exists(side));
    CHECK_FALSE(fs::exists(p.parent_path() / "renamed.txt.mod"));
    // The same history under the old name: not loaded, so the file opens with no undo.
    fs::rename(side, p.parent_path() / "renamed.txt.mod");
    auto doc = Document::open(p, q, persisted());
    REQUIRE(doc);
    REQUIRE(wait_for_root_hash(q, **doc));
    CHECK_FALSE((*doc)->undo());
}

TEST_CASE("session edits made while verifying are re-rooted on a mismatch") {
    const fs::path p = scratch("reroot.txt");
    EventQueue q({});
    // A history whose save point has the same size as the file but other content.
    write_file(p, "one\n");
    {
        auto doc = Document::open(p, q, persisted());
        REQUIRE(doc);
        REQUIRE(wait_for_root_hash(q, **doc));
        type(**doc, 0, "X");
        REQUIRE((*doc)->save());
    }
    write_file(p, "Yone\n");  // same size as the saved "Xone\n"
    auto doc = Document::open(p, q, persisted());
    REQUIRE(doc);
    Document& d = **doc;
    CHECK(d.history_state() == HistoryState::verifying);
    const NodeId candidate = *d.history().current();
    type(d, 0, "!");  // before the scan result is processed
    const NodeId session = *d.history().current();
    REQUIRE(d.undo());  // the session's own node can be undone...
    CHECK(d.undo().error().code == ErrorCode::unsupported);  // ...but not the unverified history
    REQUIRE(d.redo());
    REQUIRE(pump(q, [&] { return d.history_state() != HistoryState::verifying; }));
    CHECK(d.history_state() == HistoryState::attached);
    const NodeId root = d.history().meta(session).parent;
    CHECK(root != candidate);
    CHECK(d.history().meta(root).parent == kNoParent);
    CHECK(root < session);
    CHECK(d.history().root_base(root)->hash == hash_of("Yone\n"));
    REQUIRE(d.undo());
    CHECK(content(d) == "Yone\n");
    CHECK(d.undo().error().code == ErrorCode::canceled);  // the new root
    CHECK_FALSE(d.is_dirty());

    // The records reached the sidecar with their final parents.
    REQUIRE(d.redo());
    REQUIRE(d.save());
    UndoTree loaded;
    Sidecar reader(p, q, 0644);
    REQUIRE(reader.open(loaded, 6));
    CHECK(loaded.contains(root));
    CHECK(loaded.meta(session).parent == root);
}

TEST_CASE("a verified match attaches and keeps the old history reachable") {
    const fs::path p = scratch("match.txt");
    EventQueue q({});
    write_file(p, "one\n");
    {
        auto doc = Document::open(p, q, persisted());
        REQUIRE(doc);
        REQUIRE(wait_for_root_hash(q, **doc));
        type(**doc, 0, "X");
        REQUIRE((*doc)->save());
    }
    auto doc = Document::open(p, q, persisted());
    REQUIRE(doc);
    Document& d = **doc;
    REQUIRE(pump(q, [&] { return d.history_state() != HistoryState::verifying; }));
    CHECK(d.history_state() == HistoryState::attached);
    CHECK_FALSE(d.is_dirty());
    REQUIRE(d.undo());
    CHECK(content(d) == "one\n");
    CHECK(d.is_dirty());
    REQUIRE(d.redo());
    CHECK_FALSE(d.is_dirty());
}

TEST_CASE("reload starts a root that undo cannot leave, and abandons a pending verification") {
    const fs::path p = scratch("reload.txt");
    EventQueue q({});
    write_file(p, "one\n");
    {
        auto doc = Document::open(p, q, persisted());
        REQUIRE(doc);
        REQUIRE(wait_for_root_hash(q, **doc));
        type(**doc, 0, "X");
        REQUIRE((*doc)->save());
    }
    auto doc = Document::open(p, q, persisted());
    REQUIRE(doc);
    Document& d = **doc;
    CHECK(d.history_state() == HistoryState::verifying);
    replace_file(p, "replaced\n");  // the scan of the old file may still be running
    REQUIRE(d.reload());
    CHECK(d.history_state() == HistoryState::attached);  // verification abandoned
    CHECK(content(d) == "replaced\n");
    const NodeId root = *d.history().current();
    CHECK(d.history().meta(root).parent == kNoParent);
    CHECK(d.undo().error().code == ErrorCode::canceled);
    type(d, 0, "!");
    const NodeId edit = *d.history().current();
    d.apply(0, 0, std::string_view("P"), EditKind::paste, 0, 1);  // closes the typing node
    // Until the new scan's hash is processed, the ROOT (and what follows it) is held back.
    UndoTree before;
    {
        Sidecar reader(p, q, 0644);
        REQUIRE(reader.open(before, 9));
    }
    CHECK_FALSE(before.contains(root));
    CHECK_FALSE(before.contains(edit));
    REQUIRE(pump(q, [&] { return d.history().root_base(root)->hash == hash_of("replaced\n"); }));
    REQUIRE(d.save());
    UndoTree after;
    Sidecar reader(p, q, 0644);
    REQUIRE(reader.open(after, 11));
    CHECK(after.contains(root));
    CHECK(after.meta(edit).parent == root);
}

TEST_CASE("clear_history") {
    const fs::path p = scratch("clear.txt");
    EventQueue q({});
    write_file(p, "one\n");
    auto doc = Document::open(p, q, persisted());
    REQUIRE(doc);
    Document& d = **doc;
    REQUIRE(wait_for_root_hash(q, d));
    type(d, 0, "secret ");
    CHECK(d.clear_history().error().code == ErrorCode::internal);  // dirty
    REQUIRE(d.save());
    REQUIRE(fs::exists(sidecar_path_for(p)));

    SUBCASE("on a clean document: one root, still clean, undo refused at it") {
        REQUIRE(d.clear_history());
        CHECK_FALSE(fs::exists(sidecar_path_for(p)));
        CHECK_FALSE(d.is_dirty());
        CHECK(d.history().roots().size() == 1);
        CHECK_FALSE(d.history().retired_roots().empty());
        const NodeId root = *d.history().current();
        CHECK(d.history().root_base(root)->hash == hash_of("secret one\n"));
        CHECK(d.undo().error().code == ErrorCode::canceled);
        CHECK(content(d) == "secret one\n");
    }
    SUBCASE("refused in read_only") {
        auto other = Document::open(p, q, persisted());
        REQUIRE(other);
        CHECK((*other)->history_state() == HistoryState::read_only);
        REQUIRE(pump(q, [&] { return !(*other)->is_dirty(); }));
        CHECK((*other)->clear_history().error().code == ErrorCode::unsupported);
        CHECK(fs::exists(sidecar_path_for(p)));
    }
}

TEST_CASE("open_untitled") {
    EventQueue q({});
    auto doc = Document::open_untitled(q, persisted());
    CHECK(doc->is_untitled());
    CHECK(doc->text().size() == 0);
    CHECK_FALSE(doc->is_dirty());
    CHECK(doc->history_state() == HistoryState::session_only);
    const NodeId root = *doc->history().current();
    CHECK(doc->history().root_base(root)->hash == hash_of(""));
    CHECK(to_hex(hash_of("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(doc->save().error().code == ErrorCode::unsupported);
    type(*doc, 0, "new");
    CHECK(doc->is_dirty());

    SUBCASE("the first Save As makes it named and writes the whole history") {
        const fs::path p = scratch("untitled.txt");
        REQUIRE(doc->save_as(p));
        CHECK_FALSE(doc->is_untitled());
        CHECK(doc->path() == fs::canonical(p));
        CHECK_FALSE(doc->is_dirty());
        CHECK(doc->history_state() == HistoryState::attached);
        REQUIRE(doc->save());
        UndoTree loaded;
        Sidecar reader(p, q, 0644);
        auto out = reader.open(loaded, 3);
        REQUIRE(out);
        CHECK(loaded.contains(root));
        CHECK(loaded.contains(*doc->history().current()));
    }
}

// ---- pruning ---------------------------------------------------------------------------

namespace {

constexpr std::int64_t kOld = 100;     // too old for kCutoff
constexpr std::int64_t kCutoff = 500;

NodeMeta at_time(std::int64_t t, std::uint64_t cursor_before = 0) { return NodeMeta{0, 0, t, EditKind::typing, cursor_before, 0}; }

// R ── a ── b ──┬── c ── d          too old: a, b, e     recent: c, d, f
//               └── e ── f          leaves: d, f         current: d; latest save point: c
struct WorkedExample {
    UndoTree t;
    NodeId r, a, b, c, d, e, f;
    WorkedExample() {
        r = t.add_root({}, 0, hash_of(""));
        a = t.commit({ins(0, "a")}, at_time(kOld));
        b = t.commit({ins(1, "b")}, at_time(kOld));
        c = t.commit({ins(2, "c")}, at_time(1000));
        t.mark_saved(c, 3, hash_of("abc"));
        d = t.commit({ins(3, "d")}, at_time(1100));
        t.undo_step();
        t.undo_step();
        e = t.commit({ins(2, "e")}, at_time(kOld, 7));
        f = t.commit({ins(3, "f")}, at_time(900, 8));
        t.set_position(d, {});
    }
};

std::vector<NodeId> ids(std::initializer_list<NodeId> l) { return std::vector<NodeId>(l); }

}  // namespace

TEST_CASE("plan_prune: the worked example") {
    WorkedExample w;
    const PrunePlan plan = w.t.plan_prune(kCutoff);
    CHECK(plan.kept == ids({w.c, w.d, w.f}));
    CHECK(plan.tops == ids({w.c, w.f}));
    CHECK(plan.anchor == w.b);
    CHECK_FALSE(plan.anchor_is_root);
    CHECK(plan.root_time_ms == 900);  // the older top, f
    CHECK(plan.removed_count == 3);   // R, a and e
    CHECK(plan.removed_trees == 0);

    SUBCASE("for_each_pruned: ascending ids, concatenated runs, the first absorbed cursor_before") {
        std::vector<PrunedNode> seen;
        w.t.for_each_pruned(plan, [&](const PrunedNode& n) {
            seen.push_back(n);
            return true;
        });
        REQUIRE(seen.size() == 3);
        CHECK(seen[0].meta.id == w.c);
        CHECK(seen[0].meta.parent == w.b);
        CHECK(seen[0].op_runs.size() == 1);
        CHECK(seen[1].meta.id == w.d);
        CHECK(seen[1].meta.parent == w.c);
        CHECK(seen[2].meta.id == w.f);
        CHECK(seen[2].meta.parent == w.b);
        REQUIRE(seen[2].op_runs.size() == 2);
        CHECK(seen[2].op_runs[0].data() == w.t.ops(w.e).data());
        CHECK(seen[2].op_runs[1].data() == w.t.ops(w.f).data());
        CHECK(seen[2].meta.cursor_before == 7);
        CHECK(seen[2].meta.time_unix_ms == 900);
    }

    SUBCASE("apply_prune") {
        const bool at_saved = w.t.is_at_saved();
        const NodeId next = w.t.next_id();
        w.t.apply_prune(plan, 2, hash_of("ab"));
        CHECK(w.t.roots() == ids({w.b}));
        CHECK(w.t.meta(w.b).parent == kNoParent);
        CHECK(w.t.meta(w.b).time_unix_ms == 900);
        CHECK(w.t.root_base(w.b)->hash == hash_of("ab"));
        CHECK(w.t.ops(w.b).empty());
        CHECK_FALSE(w.t.contains(w.r));
        CHECK_FALSE(w.t.contains(w.a));
        CHECK_FALSE(w.t.contains(w.e));
        CHECK_FALSE(w.t.is_retired(w.a));  // deleted, not retired
        CHECK(w.t.node_info(w.b).children.size() == 2);
        CHECK(w.t.node_info(w.b).preferred_child == w.c);  // the top on the path to current
        CHECK(w.t.node_info(w.c).preferred_child == w.d);
        REQUIRE(w.t.ops(w.f).size() == 2);
        CHECK(std::get<Payload::Inline>(w.t.ops(w.f)[0].inserted.form).bytes == "e");
        CHECK(std::get<Payload::Inline>(w.t.ops(w.f)[1].inserted.form).bytes == "f");
        CHECK(w.t.meta(w.f).cursor_before == 7);
        CHECK(w.t.is_at_saved() == at_saved);
        CHECK(w.t.saved_node() == w.c);
        CHECK(w.t.current() == w.d);
        CHECK(w.t.next_id() == next);
        CHECK(w.t.path(w.d, w.f).size() == 3);
        // Stale rebinds: a deleted node, and an op index past the last op.
        w.t.rebind_payload(w.a, 0, Which::inserted, SidecarRef{100, 1});
        w.t.rebind_payload(w.c, 5, Which::inserted, SidecarRef{100, 1});
        CHECK(std::holds_alternative<Payload::Inline>(w.t.ops(w.c)[0].inserted.form));
        // A new edit still gets a fresh id.
        CHECK(w.t.commit({ins(0, "z")}, {}) == next);
    }
}

TEST_CASE("plan_prune: structure") {
    SUBCASE("a single kept chain anchors at its top's parent") {
        UndoTree t;
        t.add_root({}, 0, hash_of(""));
        t.commit({ins(0, "a")}, at_time(kOld));
        const NodeId b = t.commit({ins(0, "b")}, at_time(kOld));
        const NodeId c = t.commit({ins(0, "c")}, at_time(1000));
        const NodeId d = t.commit({ins(0, "d")}, at_time(1000));
        t.mark_saved(d, 4, {});
        const PrunePlan plan = t.plan_prune(kCutoff);
        CHECK(plan.kept == ids({c, d}));
        CHECK(plan.tops == ids({c}));
        CHECK(plan.anchor == b);
    }
    SUBCASE("recent -> old -> recent flattens to sibling tops under an anchor above both") {
        UndoTree t;
        t.add_root({}, 0, hash_of(""));
        const NodeId a = t.commit({ins(0, "a")}, at_time(kOld));
        const NodeId c1 = t.commit({ins(0, "1")}, at_time(1000));
        const NodeId o = t.commit({ins(0, "o")}, at_time(kOld));
        const NodeId c2 = t.commit({ins(0, "2")}, at_time(1000));
        t.undo_step();
        t.undo_step();
        const NodeId x = t.commit({ins(0, "x")}, at_time(1000));
        t.mark_saved(x, 3, {});
        const PrunePlan plan = t.plan_prune(kCutoff);
        CHECK(plan.kept == ids({c1, c2, x}));
        CHECK(plan.tops == ids({c1, c2}));
        CHECK(plan.anchor == a);
        std::vector<std::size_t> runs;
        t.for_each_pruned(plan, [&](const PrunedNode& n) {
            runs.push_back(n.op_runs.size());
            return true;
        });
        CHECK(runs == std::vector<std::size_t>{1, 3, 1});  // c2 absorbs c1 and o
        t.apply_prune(plan, 1, hash_of("a"));
        CHECK(t.node_info(a).children.size() == 2);
        CHECK(t.meta(c2).parent == a);
        CHECK(t.ops(c2).size() == 3);
        CHECK(t.ops(c1).size() == 1);  // a kept node keeps its own ops
        CHECK(t.meta(x).parent == c1);
        CHECK_FALSE(t.contains(o));
    }
    SUBCASE("a walk stops at an already kept node") {
        UndoTree t;
        const NodeId r = t.add_root({}, 0, hash_of(""));
        const NodeId a = t.commit({ins(0, "a")}, at_time(1000));
        const NodeId b = t.commit({ins(1, "b")}, at_time(1000));
        t.undo_step();
        const NodeId c = t.commit({ins(1, "c")}, at_time(1000));
        t.mark_saved(c, 2, {});
        const PrunePlan plan = t.plan_prune(kCutoff);
        CHECK(plan.kept == ids({a, b, c}));
        CHECK(plan.tops == ids({a}));
        CHECK(plan.anchor == r);
        CHECK(plan.removed_count == 0);
    }
    SUBCASE("a leaf too old for the cutoff contributes nothing") {
        UndoTree t;
        t.add_root({}, 0, hash_of(""));
        const NodeId a = t.commit({ins(0, "a")}, at_time(1000));
        const NodeId b = t.commit({ins(1, "b")}, at_time(kOld));
        t.undo_step();
        const NodeId c = t.commit({ins(1, "c")}, at_time(1000));
        t.mark_saved(c, 2, {});
        const PrunePlan plan = t.plan_prune(kCutoff);
        CHECK(plan.kept == ids({a, c}));
        CHECK(plan.removed_count == 1);
        t.apply_prune(plan, 0, hash_of(""));
        CHECK_FALSE(t.contains(b));
    }
    SUBCASE("an old current and an old latest save point are kept without their old ancestors") {
        UndoTree t;
        t.add_root({}, 0, hash_of(""));
        const NodeId a = t.commit({ins(0, "a")}, at_time(kOld));
        const NodeId b = t.commit({ins(0, "b")}, at_time(kOld));
        t.mark_saved(b, 2, {});
        t.commit({ins(0, "c")}, at_time(kOld));
        const NodeId d = t.commit({ins(0, "d")}, at_time(kOld));
        const PrunePlan plan = t.plan_prune(kCutoff);
        CHECK(plan.kept == ids({b, d}));
        CHECK(plan.tops == ids({b, d}));
        CHECK(plan.anchor == a);
    }
    SUBCASE("current at the root anchors at the root") {
        UndoTree t;
        const NodeId r = t.add_root({}, 0, hash_of(""));
        t.mark_saved(r, 0, hash_of(""));
        t.commit({ins(0, "a")}, at_time(kOld));
        t.commit({ins(0, "b")}, at_time(1000));
        t.undo_step();
        t.undo_step();
        const PrunePlan plan = t.plan_prune(kCutoff);
        CHECK(plan.anchor == r);
        CHECK(plan.anchor_is_root);
    }
    SUBCASE("a cutoff older than every node removes nothing, or exactly the other trees") {
        UndoTree t;
        t.add_root({}, 0, hash_of(""));
        t.commit({ins(0, "a")}, at_time(1000));
        const PrunePlan none = t.plan_prune(0);
        CHECK(none.removed_count == 0);
        const NodeId r2 = t.add_root({}, 0, hash_of(""));
        const NodeId b = t.commit({ins(0, "b")}, at_time(2000));
        const PrunePlan other = t.plan_prune(0);
        CHECK(other.removed_count == 2);
        CHECK(other.removed_trees == 1);
        CHECK(other.kept == ids({b}));
        CHECK(other.anchor == r2);
        t.apply_prune(other, 0, hash_of(""));
        CHECK(t.roots() == ids({r2}));
    }
    SUBCASE("after a load that matched no save point, oldest_change looks at the other trees") {
        UndoTree t;
        REQUIRE(t.load_root(NodeMeta{1, kNoParent, 10, EditKind::other, 0, 0}, 0, hash_of("")));
        REQUIRE(t.load_node(NodeMeta{2, 1, 50, EditKind::typing, 0, 1}, {ins(0, "a")}));
        REQUIRE(t.load_node(NodeMeta{3, 2, 70, EditKind::typing, 1, 2}, {ins(1, "b")}));
        t.add_root(NodeMeta{0, kNoParent, 1000, EditKind::other, 0, 0}, 5, hash_of("other"));
        CHECK(t.oldest_change() == 50);
        const PrunePlan plan = t.plan_prune(0);
        CHECK(plan.removed_count == 3);
        CHECK(plan.removed_trees == 1);
    }
    SUBCASE("retired trees from Clear History are untouched") {
        UndoTree t;
        t.add_root({}, 0, hash_of(""));
        const NodeId old = t.commit({ins(0, "a")}, at_time(1));
        t.reset();
        t.add_root({}, 1, hash_of("a"));
        t.commit({ins(0, "b")}, at_time(kOld));
        const NodeId c = t.commit({ins(0, "c")}, at_time(1000));
        const auto retired = t.retired_roots();
        CHECK(t.oldest_change() == kOld);  // the live b; the retired node at time 1 does not count
        const PrunePlan plan = t.plan_prune(kCutoff);
        t.apply_prune(plan, 2, hash_of("ba"));
        CHECK(t.retired_roots() == retired);
        CHECK(t.is_retired(old));
        CHECK(t.meta(old).time_unix_ms == 1);
        CHECK(t.current() == c);
    }
}

// ---- Document.prune_history -------------------------------------------------------------

namespace {

struct ChangeCounter : DocumentListener {
    int changes = 0;
    void before_change(const ChangeEvent&) override { ++changes; }
    void after_change(const ChangeEvent&) override { ++changes; }
};

void paste(Document& d, std::uint64_t at, std::string_view s) { d.apply(at, 0, s, EditKind::paste, at, at + s.size()); }

// "base\n", then: a pastes 5000 x's (old), b pastes "B" (old); ten days later c deletes
// the x's, d pastes "D"; undo d, e pastes "E" and is saved. Kept: c, d, e; anchor b.
struct PruneFixture {
    fs::path p;
    EventQueue q{{}};
    Clock clock;
    std::unique_ptr<Document> doc;
    NodeId r = 0, a = 0, b = 0, c = 0, d = 0, e = 0;

    explicit PruneFixture(const std::string& name) : p(scratch(name)) {
        write_file(p, "base\n");
        auto opened = Document::open(p, q, clock.options());
        REQUIRE(opened);
        doc = std::move(*opened);
        Document& x = *doc;
        REQUIRE(wait_for_root_hash(q, x));
        r = *x.history().current();
        paste(x, 0, std::string(5000, 'x'));
        a = *x.history().current();
        clock.advance(1000);
        paste(x, 0, "B");
        b = *x.history().current();
        clock.advance(10 * kDayMs);
        x.apply(1, 5000, std::string_view(), EditKind::delete_, 1, 1);
        c = *x.history().current();
        clock.advance(1000);
        paste(x, 0, "D");  // closes c, which reaches the sidecar
        d = *x.history().current();
        // c's removed bytes become a SidecarRef, so the silent walk reads the old mapping.
        REQUIRE(pump(q, [&] { return std::holds_alternative<SidecarRef>(x.history().ops(c)[0].removed.form); }));
        REQUIRE(x.undo());
        clock.advance(1000);
        paste(x, 0, "E");
        e = *x.history().current();
        REQUIRE(x.save());
    }
};

}  // namespace

TEST_CASE("Document.prune_history") {
    PruneFixture fx("prune.txt");
    Document& d = *fx.doc;
    REQUIRE(d.history_state() == HistoryState::attached);
    const fs::path side = sidecar_path_for(fx.p);

    SUBCASE("prune_preview reports the oldest age in whole days and the counts") {
        auto only_age = d.prune_preview(std::nullopt);
        REQUIRE(only_age);
        CHECK(only_age->oldest_days == 10);
        CHECK(only_age->remove_count == 0);
        auto preview = d.prune_preview(5);
        REQUIRE(preview);
        CHECK(preview->cutoff_ms == *fx.clock.now - 5 * kDayMs);
        CHECK(preview->remove_count == 2);  // the old root and a; b becomes the root
        CHECK(preview->keep_count == 3);
        CHECK(preview->removed_trees == 0);
    }

    SUBCASE("content, jumps and listeners") {
        const std::string before = content(d);
        std::map<NodeId, std::string> at;
        for (const NodeId n : {fx.b, fx.c, fx.d, fx.e}) {
            REQUIRE(d.jump_to(n));
            at[n] = content(d);
        }
        REQUIRE(d.jump_to(fx.e));
        REQUIRE(content(d) == before);
        CHECK(d.history().save_point(fx.b) == std::nullopt);  // so the anchor needs the silent walk

        ChangeCounter counter;
        d.add_listener(&counter);
        const std::uint64_t version = d.version();
        const auto side_before = fs::file_size(side);
        REQUIRE(d.prune_history(*fx.clock.now - 5 * kDayMs));
        d.remove_listener(&counter);
        CHECK(counter.changes == 0);
        CHECK(d.version() == version);
        CHECK(content(d) == before);
        CHECK_FALSE(d.is_dirty());
        CHECK(fs::file_size(side) < side_before);

        CHECK(d.history().roots() == std::vector<NodeId>{fx.b});
        CHECK_FALSE(d.history().contains(fx.a));
        CHECK_FALSE(d.history().contains(fx.r));
        CHECK(d.history().root_base(fx.b)->hash == hash_of(at[fx.b]));
        CHECK(d.history().root_base(fx.b)->size == at[fx.b].size());

        // Every pair of kept nodes, including the new root, jumps as it did before.
        for (const auto& [from, ignored] : at) {
            for (const auto& [to, expected] : at) {
                REQUIRE(d.jump_to(from));
                REQUIRE(d.jump_to(to));
                CHECK(content(d) == expected);
            }
        }
        REQUIRE(d.jump_to(fx.e));
        CHECK_FALSE(d.is_dirty());
        CHECK(d.undo().has_value());
        CHECK(d.undo().has_value());
        CHECK(d.undo().error().code == ErrorCode::canceled);  // at the new root
    }

    SUBCASE("a rewrite failure leaves the tree and the file unchanged") {
        const fs::path link = fx.p.parent_path() / "prune-link.mod";
        fs::remove(link);
        fs::create_hard_link(side, link);
        // A refusal first drains the history writer (records still queued are written), so
        // the file is read once nothing is pending: the second refusal must not touch it.
        REQUIRE_FALSE(d.prune_history(*fx.clock.now - 5 * kDayMs));
        const std::string file_before = read_back(side);
        const auto roots = d.history().roots();
        auto st = d.prune_history(*fx.clock.now - 5 * kDayMs);
        REQUIRE_FALSE(st);
        CHECK(st.error().code == ErrorCode::not_atomic);
        CHECK(read_back(side) == file_before);
        CHECK(d.history().roots() == roots);
        CHECK(d.history().contains(fx.a));
        CHECK(d.history().meta(fx.c).parent == fx.b);
        fs::remove(link);
    }

    SUBCASE("refused while verifying and read_only, with nothing changed") {
        auto other = Document::open(fx.p, fx.q, fx.clock.options());
        REQUIRE(other);
        Document& o = **other;
        CHECK(o.history_state() == HistoryState::read_only);
        CHECK(o.prune_history(*fx.clock.now).error().message == "history is open in another mod");
        CHECK(o.prune_preview(1).error().code == ErrorCode::unsupported);
        fx.doc.reset();  // releases the lock
        other->reset();
        auto again = Document::open(fx.p, fx.q, fx.clock.options());
        REQUIRE(again);
        CHECK((*again)->history_state() == HistoryState::verifying);
        const std::string file_before = read_back(side);
        CHECK((*again)->prune_history(*fx.clock.now).error().message == "history is still being verified");
        CHECK(read_back(side) == file_before);
        CHECK((*again)->history().contains(fx.a));
    }
}

TEST_CASE("Document.prune_history is refused when history is not being saved") {
    EventQueue q({});
    auto doc = Document::open_untitled(q);
    type(*doc, 0, "x");
    CHECK(doc->history_state() == HistoryState::session_only);
    auto st = doc->prune_history(0);
    REQUIRE_FALSE(st);
    CHECK(st.error().code == ErrorCode::unsupported);
    CHECK(st.error().message == "history is kept in memory only: turn on Persist History (P) to trim it");
    CHECK(doc->prune_preview(std::nullopt).error().message == "history is kept in memory only: turn on Persist History (P) to trim it");
}

TEST_CASE("a document opened with history off never reads or writes its sidecar") {
    const fs::path p = scratch("nohistory.txt");
    EventQueue q({});
    write_file(p, "one\n");
    {
        auto doc = Document::open(p, q, persisted());  // an ordinary open writes a history
        REQUIRE(doc);
        REQUIRE(wait_for_root_hash(q, **doc));
        type(**doc, 0, "X");
        REQUIRE((*doc)->save());
    }
    const auto before = fs::file_size(sidecar_path_for(p));
    DocumentOptions options;
    options.history = false;
    auto doc = Document::open(p, q, options);
    REQUIRE(doc);
    CHECK((*doc)->history_state() == HistoryState::session_only);
    CHECK((*doc)->take_status_message().empty());  // not reported as a failure
    CHECK(content(**doc) == "Xone\n");
    type(**doc, 0, "Y");
    doc->reset();
    CHECK(fs::file_size(sidecar_path_for(p)) == before);
    const fs::path fresh = scratch("nohistory_new.txt");
    write_file(fresh, "a\n");
    auto other = Document::open(fresh, q, options);
    REQUIRE(other);
    other->reset();
    CHECK_FALSE(fs::exists(sidecar_path_for(fresh)));
}

// ---- Persist History -----------------------------------------------------------------------

namespace {

// Opens `p`, waits for its history to settle, types `text` at 0 and saves.
void edit_and_save(const fs::path& p, EventQueue& q, std::string_view text, DocumentOptions options = {}) {
    auto doc = Document::open(p, q, std::move(options));
    REQUIRE(doc);
    REQUIRE(pump(q, [&] { return (*doc)->history_state() != HistoryState::verifying; }));
    REQUIRE(wait_for_root_hash(q, **doc));
    type(**doc, 0, text);
    REQUIRE((*doc)->save());
}

}  // namespace

TEST_CASE("history is kept in memory by default: editing and saving create no sidecar") {
    const fs::path p = scratch("memory_only.txt");
    EventQueue q({});
    write_file(p, "one\n");
    auto doc = Document::open(p, q);
    REQUIRE(doc);
    Document& d = **doc;
    REQUIRE(wait_for_root_hash(q, d));
    CHECK_FALSE(d.persist_history());
    CHECK_FALSE(d.history_unreadable());
    CHECK(d.history_state() == HistoryState::session_only);
    type(d, 0, "X");
    REQUIRE(d.save());
    REQUIRE(d.undo());
    REQUIRE(d.redo());
    doc->reset();
    CHECK_FALSE(fs::exists(sidecar_path_for(p)));
}

TEST_CASE("Persist History writes the whole session's history; a reopen restores it with persistence on") {
    const fs::path p = scratch("persist_on.txt");
    EventQueue q({});
    write_file(p, "one\n");
    {
        auto doc = Document::open(p, q);
        REQUIRE(doc);
        Document& d = **doc;
        REQUIRE(wait_for_root_hash(q, d));
        type(d, 0, "A");
        type(d, 1, "B");
        REQUIRE(d.save());
        REQUIRE(d.set_persist_history(true));
        CHECK(d.persist_history());
        CHECK(d.history_state() == HistoryState::attached);
    }
    REQUIRE(fs::exists(sidecar_path_for(p)));
    auto doc = Document::open(p, q);
    REQUIRE(doc);
    Document& d = **doc;
    CHECK(d.persist_history());
    REQUIRE(pump(q, [&] { return d.history_state() != HistoryState::verifying; }));
    CHECK(d.history_state() == HistoryState::attached);
    REQUIRE(d.undo());  // the session made before persistence was on came back
    CHECK(content(d) != "ABone\n");
}

TEST_CASE("turning persistence off stops writing and keeps the file") {
    const fs::path p = scratch("persist_off.txt");
    EventQueue q({});
    write_file(p, "one\n");
    edit_and_save(p, q, "A", persisted());
    REQUIRE(fs::exists(sidecar_path_for(p)));
    const std::string before = read_back(sidecar_path_for(p));
    {
        auto doc = Document::open(p, q);
        REQUIRE(doc);
        Document& d = **doc;
        REQUIRE(pump(q, [&] { return d.history_state() != HistoryState::verifying; }));
        CHECK(d.persist_history());
        REQUIRE(d.set_persist_history(false));
        CHECK_FALSE(d.persist_history());
        CHECK(d.history_state() == HistoryState::session_only);
        type(d, 0, "Z");
        REQUIRE(d.save());
        REQUIRE(d.undo());  // a step stored in the file still undoes
        REQUIRE(d.undo());
    }
    CHECK(read_back(sidecar_path_for(p)) == before);
}

TEST_CASE("an unreadable sidecar: persistence off, turning it on needs an overwrite, which writes the session") {
    const fs::path p = scratch("unreadable.txt");
    EventQueue q({});
    write_file(p, "one\n");
    write_file(sidecar_path_for(p), "not a history file");
    {
        auto doc = Document::open(p, q);
        REQUIRE(doc);
        Document& d = **doc;
        REQUIRE(wait_for_root_hash(q, d));
        CHECK_FALSE(d.persist_history());
        CHECK(d.history_unreadable());
        type(d, 0, "A");
        REQUIRE(d.save());
        const auto refused = d.set_persist_history(true);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().code == ErrorCode::format);
        CHECK(read_back(sidecar_path_for(p)) == "not a history file");
        REQUIRE(d.set_persist_history(true, true));
        CHECK(d.persist_history());
        CHECK_FALSE(d.history_unreadable());
    }
    auto doc = Document::open(p, q);
    REQUIRE(doc);
    CHECK((*doc)->persist_history());
    REQUIRE(pump(q, [&] { return (*doc)->history_state() != HistoryState::verifying; }));
    REQUIRE((*doc)->undo());
    CHECK(content(**doc) == "one\n");
}

TEST_CASE("a sidecar locked by another mod cannot be switched") {
    const fs::path p = scratch("locked.txt");
    EventQueue q({});
    write_file(p, "one\n");
    edit_and_save(p, q, "A", persisted());
    auto first = Document::open(p, q);
    REQUIRE(first);
    auto second = Document::open(p, q);
    REQUIRE(second);
    CHECK((*second)->history_state() == HistoryState::read_only);
    const auto refused = (*second)->set_persist_history(false);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == ErrorCode::unsupported);
}

TEST_CASE("untitled: persistence is remembered until the first save; without it Save As makes no sidecar") {
    EventQueue q({});
    const fs::path with = scratch("untitled_persisted.txt");
    {
        auto doc = Document::open_untitled(q);
        REQUIRE(doc);
        REQUIRE(doc->set_persist_history(true));
        type(*doc, 0, "hello");
        REQUIRE(doc->save_as(with));
    }
    CHECK(fs::exists(sidecar_path_for(with)));
    const fs::path without = scratch("untitled_memory.txt");
    {
        auto doc = Document::open_untitled(q);
        REQUIRE(doc);
        type(*doc, 0, "hello");
        REQUIRE(doc->save_as(without));
        CHECK_FALSE(doc->persist_history());
    }
    CHECK_FALSE(fs::exists(sidecar_path_for(without)));
}

TEST_CASE("Save As without persistence makes no sidecar; turning it on afterwards writes at the new path") {
    EventQueue q({});
    const fs::path a = scratch("saveas_from.txt");
    const fs::path b = scratch("saveas_to.txt");
    write_file(a, "one\n");
    {
        auto doc = Document::open(a, q);
        REQUIRE(doc);
        Document& d = **doc;
        REQUIRE(wait_for_root_hash(q, d));
        type(d, 0, "A");
        REQUIRE(d.save_as(b));
        CHECK_FALSE(fs::exists(sidecar_path_for(b)));
        REQUIRE(d.set_persist_history(true));
    }
    CHECK_FALSE(fs::exists(sidecar_path_for(a)));
    CHECK(fs::exists(sidecar_path_for(b)));
    auto doc = Document::open(b, q);
    REQUIRE(doc);
    REQUIRE(pump(q, [&] { return (*doc)->history_state() != HistoryState::verifying; }));
    REQUIRE((*doc)->undo());
}

// ---- history previews ---------------------------------------------------------------------

namespace {

void change(Document& d, std::uint64_t at, std::uint64_t remove, std::string_view text) {
    d.apply(at, remove, InsertContent(text), EditKind::other, at, at + text.size());
}

}  // namespace

TEST_CASE("a preview shows the text at any node without changing the history, and ends exactly") {
    const fs::path p = scratch("preview.txt");
    EventQueue q({});
    write_file(p, "base\n");
    auto doc = Document::open(p, q);
    REQUIRE(doc);
    Document& d = **doc;
    REQUIRE(wait_for_root_hash(q, d));
    change(d, 0, 0, "a");
    const NodeId na = *d.history().current();
    change(d, 1, 0, "b");
    const NodeId nb = *d.history().current();
    REQUIRE(d.undo());
    change(d, 1, 0, "c");  // a branch beside "b"
    const NodeId nc = *d.history().current();
    const NodeId root = d.history().root_of(nc);
    const std::string now = content(d);
    const std::uint64_t version = d.version();
    const std::map<NodeId, std::string> expected{{root, "base\n"}, {na, "abase\n"}, {nb, "abbase\n"}, {nc, "acbase\n"}};
    for (const auto& [node, text] : expected) {
        CAPTURE(node);
        auto marks = d.begin_preview(node);
        REQUIRE(marks);
        CHECK(d.previewing());
        CHECK(content(d) == text);  // inserts only: nothing removed is spliced in
        CHECK(*d.history().current() == nc);
        CHECK(d.version() == version);
    }
    d.end_preview();
    CHECK_FALSE(d.previewing());
    CHECK(content(d) == now);
    REQUIRE(d.undo());  // the history works as before
    CHECK(content(d) == "abase\n");
}

TEST_CASE("preview marks: an insert, a deletion spliced back in, a replacement") {
    const fs::path p = scratch("preview_marks.txt");
    EventQueue q({});
    write_file(p, "abc\n");
    auto doc = Document::open(p, q);
    REQUIRE(doc);
    Document& d = **doc;
    REQUIRE(wait_for_root_hash(q, d));
    change(d, 3, 0, "XY");
    const NodeId ins = *d.history().current();
    change(d, 0, 1, "");  // deletes the "a"
    const NodeId del = *d.history().current();
    change(d, 1, 1, "Z");  // "bcXY" → "bZXY": replaces the "c"
    const NodeId rep = *d.history().current();

    auto m = d.begin_preview(ins);
    REQUIRE(m);
    CHECK(content(d) == "abcXY\n");
    CHECK(*m == std::vector<PreviewMark>{{3, 5, false}});
    m = d.begin_preview(del);
    REQUIRE(m);
    CHECK(content(d) == "abcXY\n");  // the deleted "a" is back for display
    CHECK(*m == std::vector<PreviewMark>{{0, 1, true}});
    m = d.begin_preview(rep);
    REQUIRE(m);
    CHECK(content(d) == "bcZXY\n");  // the removed "c", then the inserted "Z"
    CHECK(*m == std::vector<PreviewMark>{{1, 2, true}, {2, 3, false}});
    d.end_preview();
    CHECK(content(d) == "bZXY\n");
}

TEST_CASE("a sidecar's node ids are bounded, so a crafted one cannot wrap the id counter") {
    UndoTree t;
    CHECK_FALSE(t.load_root(NodeMeta{kNoParent - 1, kNoParent, 0, EditKind::other, 0, 0}, 0, ContentHash{}));
    REQUIRE(t.load_root(NodeMeta{1, kNoParent, 0, EditKind::other, 0, 0}, 0, ContentHash{}));
    CHECK_FALSE(t.load_node(NodeMeta{kNoParent - 1, 1, 0, EditKind::other, 0, 0}, {}));
    CHECK_FALSE(t.load_node(NodeMeta{kMaxNodeId + 1, 1, 0, EditKind::other, 0, 0}, {}));
    REQUIRE(t.load_node(NodeMeta{kMaxNodeId, 1, 0, EditKind::other, 0, 0}, {}));
    // New nodes still get ids no node has.
    t.set_position(kMaxNodeId, {});
    const NodeId fresh = t.commit({ins(0, "x")}, NodeMeta{});
    CHECK(fresh != kMaxNodeId);
    CHECK(fresh != kNoParent);
}
