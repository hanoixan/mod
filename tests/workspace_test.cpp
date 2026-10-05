#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "app/workspace.hpp"
#include "util/event_queue.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    EventQueue q{{}};
    Workspace ws;
    fs::path dir = fs::path(MOD_TEST_SCRATCH) / "workspace_test";

    Fixture() { fs::create_directories(dir); }

    std::shared_ptr<Document> document(const std::string& name) {
        const fs::path p = dir / name;
        std::ofstream(p, std::ios::binary) << name << "\n";
        auto d = Document::open(p, q);
        REQUIRE(d);
        return std::shared_ptr<Document>(std::move(*d));
    }
    // Opens `name` into the focused view, as App does: the old document released first.
    DocumentList::Id open(const std::string& name) {
        ws.release_focused();
        const DocumentList::Id id = ws.documents().add();
        ws.focused().id = id;
        ws.focused().slot.doc = document(name);
        return id;
    }
    // A second view of the focused view's document, below it.
    void split() {
        View v;
        v.id = ws.focused().id;
        v.slot.doc = ws.focused().slot.doc;
        ws.insert_below_focus(std::move(v));
    }
    fs::path path_of(const std::string& name) { return fs::weakly_canonical(dir / name); }
};

}  // namespace

TEST_CASE("one empty view to start with") {
    Workspace ws;
    CHECK(ws.size() == 1);
    CHECK(ws.focus() == 0);
    CHECK(ws.focused().id == 0);
}

TEST_CASE("opening another document parks the first; showing it again takes it back") {
    Fixture f;
    const auto a = f.open("a.txt");
    const auto b = f.open("b.txt");
    CHECK(f.ws.focused().id == b);
    REQUIRE(f.ws.entry_of(a) != nullptr);  // parked
    f.ws.release_focused();
    REQUIRE(f.ws.take_parked(a));
    CHECK(f.ws.focused().id == a);
    CHECK(f.ws.documents().shown() == a);
}

TEST_CASE("a file shown only in another view is found, so it is never opened twice") {
    Fixture f;
    const auto a = f.open("a.txt");
    f.split();
    f.open("b.txt");  // the top view now shows b; a is only in the lower view
    CHECK(f.ws.find(f.path_of("a.txt")) == a);
    CHECK(f.ws.find(f.path_of("b.txt")).has_value());
    CHECK_FALSE(f.ws.find(f.path_of("c.txt")).has_value());
}

TEST_CASE("a view following a link still counts as showing its document: releasing it never parks it too") {
    Fixture f;
    const auto a = f.open("a.txt");
    f.split();
    // The focused view follows a link: its slot shows the target, the document waits in base.
    View& top = f.ws.focused();
    top.ro.away = true;
    top.ro.base = std::move(top.slot);
    top.slot.doc = f.document("target.md");
    f.ws.focus_on(1);  // the lower view, also on a
    f.ws.release_focused();  // the top view still shows a: nothing parked
    CHECK(f.ws.entry_of(a) == &f.ws.at(0));
    f.ws.focus_on(0);
    f.ws.release_focused();  // now nothing else shows a: parked, with its followed link
    const View* parked = f.ws.entry_of(a);
    REQUIRE(parked != nullptr);
    CHECK(parked->ro.away);
    CHECK(Workspace::document_of(*parked)->path().filename() == "a.txt");
}

TEST_CASE("removing the focused view moves the focus up, else down") {
    Fixture f;
    f.open("a.txt");
    f.split();
    f.split();  // three views of a, focus on the top
    f.ws.focus_on(2);
    f.ws.release_focused();
    f.ws.remove_focused();
    CHECK(f.ws.size() == 2);
    CHECK(f.ws.focus() == 1);
    f.ws.focus_on(0);
    f.ws.release_focused();
    f.ws.remove_focused();
    CHECK(f.ws.size() == 1);
    CHECK(f.ws.focus() == 0);
}

TEST_CASE("trimming views parks only documents no remaining view shows") {
    Fixture f;
    const auto a = f.open("a.txt");
    f.split();  // views: a, a
    f.ws.focus_on(1);
    const auto b = f.open("b.txt");  // views: a, b
    f.ws.focus_on(0);
    f.split();  // views: a, a, b
    f.ws.trim(2);  // b goes: parked
    CHECK(f.ws.size() == 2);
    REQUIRE(f.ws.entry_of(b) != nullptr);
    CHECK(f.ws.other_view_of(b) == std::nullopt);
    f.ws.focus_on(1);
    f.ws.trim(1);  // the focused bottom view goes: the focus moves up; a is still shown
    CHECK(f.ws.size() == 1);
    CHECK(f.ws.focus() == 0);
    CHECK(f.ws.entry_of(a) == &f.ws.at(0));
}

TEST_CASE("closing a document removes its other views; unsaved lists every dirty document once") {
    Fixture f;
    const auto a = f.open("a.txt");
    f.split();
    f.split();
    f.ws.focused().slot.doc->apply(0, 0, std::string_view("x"), EditKind::typing, 0, 1);
    CHECK(f.ws.unsaved() == std::vector<DocumentList::Id>{a});  // three views, one document
    f.ws.remove_other_views_of(a);
    CHECK(f.ws.size() == 1);
}

TEST_CASE("find knows a document by any name of its file: relative, through a symlink, or a hard link") {
    Fixture f;
    const auto a = f.open("named.txt");
    fs::remove(f.dir / "named-link.txt");
    fs::remove(f.dir / "named-hard.txt");
    fs::create_symlink("named.txt", f.dir / "named-link.txt");
    fs::create_hard_link(f.dir / "named.txt", f.dir / "named-hard.txt");
    CHECK(f.ws.find(f.dir / "named.txt") == a);
    CHECK(f.ws.find(f.dir / "." / "named.txt") == a);
    CHECK(f.ws.find(f.dir / "named-link.txt") == a);
    CHECK(f.ws.find(f.dir / "named-hard.txt") == a);
}

TEST_CASE("find knows a new file opened through a dangling symlink by the link's name") {
    Fixture f;
    fs::remove(f.dir / "future.txt");
    fs::remove(f.dir / "future-link.txt");
    fs::create_symlink("future.txt", f.dir / "future-link.txt");
    f.ws.release_focused();
    const DocumentList::Id id = f.ws.documents().add();
    f.ws.focused().id = id;
    auto d = Document::open(f.dir / "future-link.txt", f.q);
    REQUIRE(d);
    f.ws.focused().slot.doc = std::shared_ptr<Document>(std::move(*d));
    CHECK(f.ws.find(f.dir / "future-link.txt") == id);
    CHECK(f.ws.find(f.dir / "future.txt") == id);
}
