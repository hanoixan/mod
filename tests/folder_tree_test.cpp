#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "app/folder_tree.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

// root/ with folders b/ (holding inner.txt) and A/, hidden .git/, and files z.txt, .env, m.md.
fs::path make_root(const std::string& name) {
    const fs::path root = fs::path(MOD_TEST_SCRATCH) / "folder_tree_test" / name;
    fs::remove_all(root);
    fs::create_directories(root / "b");
    fs::create_directories(root / "A");
    fs::create_directories(root / ".git");
    for (const char* f : {"z.txt", ".env", "m.md", "b/inner.txt"}) std::ofstream(root / f) << "x\n";
    return root;
}

std::vector<std::string> names(const FolderTree& t) {
    std::vector<std::string> out;
    for (const TreeRow& r : t.rows()) out.push_back(std::string(static_cast<std::size_t>(r.depth) * 2, ' ') + r.name + (r.dir ? "/" : ""));
    return out;
}

}  // namespace

TEST_CASE("the root, open, then everything in it: folders first, then files, by name regardless of case") {
    const fs::path root = make_root("listing");
    FolderTree t(root);
    CHECK(names(t) == std::vector<std::string>{"listing/", "  .git/", "  A/", "  b/", "  .env", "  m.md", "  z.txt"});
    CHECK(t.rows()[0].expanded);
    CHECK(t.rows()[1].hidden);   // .git
    CHECK_FALSE(t.rows()[2].hidden);
    CHECK(t.selected() == 0);
}

TEST_CASE("Right opens a folder, read then; Left closes it, and the folders stay as left for the session") {
    const fs::path root = make_root("open-close");
    FolderTree t(root);
    t.select(3);  // b/
    t.expand();
    CHECK(names(t) == std::vector<std::string>{"open-close/", "  .git/", "  A/", "  b/", "    inner.txt", "  .env", "  m.md", "  z.txt"});
    std::ofstream(root / "b" / "later.txt") << "y\n";  // appears when the folder is opened again
    t.collapse();
    CHECK(names(t).size() == 7);
    t.toggle();  // Enter on a folder opens it again
    CHECK(names(t)[4] == "    inner.txt");
    CHECK(names(t)[5] == "    later.txt");
    t.select(5);    // later.txt
    t.collapse();   // on a file: its folder is selected
    CHECK(t.selected() == 3);
    t.collapse();   // closes b/
    t.collapse();   // b/ closed: its parent, the root
    CHECK(t.selected() == 0);
    t.collapse();   // the root stays open
    CHECK(t.rows()[0].expanded);
}

TEST_CASE("moving: by one, by a page, to the ends, never past either") {
    const fs::path root = make_root("moving");
    FolderTree t(root);
    t.move(-1);
    CHECK(t.selected() == 0);
    t.move(3);
    CHECK(t.selected() == 3);
    t.move(100);
    CHECK(t.selected() == t.rows().size() - 1);
    t.move(-2);
    CHECK(t.selected() == t.rows().size() - 3);
    CHECK(t.selected_row()->name == ".env");
}

TEST_CASE("a folder that cannot be read says so and stays closed") {
    const fs::path root = make_root("unreadable");
    fs::permissions(root / "A", fs::perms::none);
    FolderTree t(root);
    t.select(2);  // A/
    t.expand();
    fs::permissions(root / "A", fs::perms::owner_all);
    if (::geteuid() == 0) return;  // root reads it anyway
    CHECK_FALSE(t.rows()[2].expanded);
    CHECK(t.message().find("A") != std::string::npos);
}
