#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

#include "app/folder_tree.hpp"
#include "platform/terminal.hpp"
#include "ui/folder_tree_view.hpp"
#include "ui/screen.hpp"
#include "ui/theme.hpp"
#include "fs_probe.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

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

fs::path make_root(const std::string& name, int files) {
    const fs::path root = fs::path(MOD_TEST_SCRATCH) / "folder_tree_view_test" / name;
    fs::remove_all(root);
    fs::create_directories(root / "src");
    fs::create_directories(root / ".git");
    for (int i = 0; i < files; ++i) std::ofstream(root / ("file" + std::to_string(10 + i) + ".txt")) << "x\n";
    std::ofstream(root / "src" / "a_rather_long_file_name_that_does_not_fit.cpp") << "x\n";
    return root;
}

struct Fixture {
    NullTerminal term;
    Screen screen{term};
    FolderTree tree;
    FolderTreeView view;
    explicit Fixture(const fs::path& root, int rows = 8, int cols = 20) : tree(root) {
        screen.resize({rows, cols});
        view.set_tree(&tree);
    }
    std::string row(int r) const {
        std::string out;
        for (int c = 0; c < screen.cols(); ++c) out.append(screen.cell(r, c).utf8.data(), screen.cell(r, c).len);
        return out;
    }
    void draw(bool focused = true) { view.render(screen, Rect{0, 0, screen.rows(), screen.cols()}, focused); }
};

KeyEvent key(Key k, std::uint8_t mods = 0) { return KeyEvent{k, 0, mods}; }

}  // namespace

TEST_CASE("rows: an open folder ▾, a closed one ▸, files indented under their folder; the hints on the bottom line") {
    Fixture f(make_root("rows", 2));
    f.draw();
    CHECK(f.row(0).starts_with("▾ rows"));
    CHECK(f.row(1).starts_with("  ▸ .git"));
    CHECK(f.row(2).starts_with("  ▸ src"));
    CHECK(f.row(3).starts_with("    file10.txt"));
    CHECK(f.row(7).find("Space: view") != std::string::npos);  // the tree's own line: the short hints in 20 columns
    CHECK((f.screen.cell(1, 4).attr.flags & kDim) != 0);  // a hidden name is dim
    CHECK(f.screen.cell(0, 2).attr == attr_for(Style::list_selected));  // the selection, with the keys
    f.draw(false);
    CHECK(f.screen.cell(0, 2).attr == attr_for(Style::list_selected_unfocused));  // and without them
}

TEST_CASE("keys: arrows and pages move, Right and Left open and close, Space previews and Enter opens a file") {
    Fixture f(make_root("keys", 12));
    f.draw();
    CHECK(f.view.handle_key(key(Key::Down)).kind == TreeKey::moved);
    CHECK(f.tree.selected() == 1);
    f.view.handle_key(key(Key::Down));   // src/
    f.view.handle_key(key(Key::Right));
    CHECK(f.tree.rows()[2].expanded);
    f.view.handle_key(key(Key::Left));
    CHECK_FALSE(f.tree.rows()[2].expanded);
    f.view.handle_key(key(Key::Enter));  // Enter on a folder opens it too
    CHECK(f.tree.rows()[2].expanded);
    f.view.handle_key(key(Key::Down));   // the long name in src/
    const TreeKeyResult space = f.view.handle_key(KeyEvent{Key::Char, U' ', 0});
    CHECK(space.kind == TreeKey::preview);
    CHECK(space.path.filename() == "a_rather_long_file_name_that_does_not_fit.cpp");
    const TreeKeyResult enter = f.view.handle_key(key(Key::Enter));
    CHECK(enter.kind == TreeKey::open);
    CHECK(enter.path == space.path);
    f.view.handle_key(key(Key::PageDown));  // a page is the rows shown, less the bottom line
    CHECK(f.tree.selected() == 3 + 7);
    f.view.handle_key(key(Key::End));
    CHECK(f.tree.selected() == f.tree.rows().size() - 1);
    f.view.handle_key(key(Key::Home));
    CHECK(f.tree.selected() == 0);
    CHECK(f.view.handle_key(key(Key::Right, kShift)).kind == TreeKey::back);
    CHECK(f.view.handle_key(key(Key::Escape)).kind == TreeKey::leave);
    CHECK(f.view.handle_key(KeyEvent{Key::Char, U' ', 0}).kind == TreeKey::none);  // Space on a folder
}

TEST_CASE("the selected row stays in view, and a name too long to fit is panned into view whole") {
    Fixture f(make_root("pan", 12));
    f.draw();
    f.view.handle_key(key(Key::End));
    f.draw();
    CHECK(f.row(6).find("file21.txt") != std::string::npos);  // the last row above the bottom line
    f.view.handle_key(key(Key::Home));
    f.view.handle_key(key(Key::Down));
    f.view.handle_key(key(Key::Down));   // src/
    f.view.handle_key(key(Key::Right));
    f.view.handle_key(key(Key::Down));   // its long file
    f.draw();
    bool whole = false;
    for (int r = 0; r < 7; ++r) whole = whole || f.row(r).find("does_not_fit.cpp") != std::string::npos;
    CHECK(whole);                        // its end is in view
    f.view.handle_key(key(Key::Up));
    f.draw();
    bool unpanned = false;  // panned back for a short one: its row starts at its indent again
    for (int r = 0; r < 7; ++r) unpanned = unpanned || f.row(r).starts_with("  ▾ src");
    CHECK(unpanned);
}

TEST_CASE("the full hints when they fit; a message from the tree replaces them") {
    Fixture wide(make_root("wide", 1), 8, 60);
    wide.draw();
    CHECK(wide.row(7).find("Space: preview  Enter: open  Shift+Right: back") != std::string::npos);
}

TEST_CASE("a message from the tree replaces the hints on the bottom line") {
    const fs::path root = make_root("message", 1);
    fs::permissions(root / "src", fs::perms::none);
    Fixture f(root, 8, 40);
    f.view.handle_key(key(Key::Down));
    f.view.handle_key(key(Key::Down));
    f.view.handle_key(key(Key::Right));
    f.draw();
    fs::permissions(root / "src", fs::perms::owner_all);
    if (!probe::permissions_enforced(root)) {
        MESSAGE("skipped: permissions are not enforced here");
        return;
    }
    CHECK(f.row(7).find("cannot open src") != std::string::npos);
}
