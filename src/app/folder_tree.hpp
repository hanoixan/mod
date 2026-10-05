#pragma once

#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace mod {

// One shown row of the folder tree.
struct TreeRow {
    std::filesystem::path path;
    std::string name;
    int depth = 0;  // the root is 0
    bool dir = false;
    bool expanded = false;
    bool hidden = false;  // the name starts with '.'
};

// The folder tree's contents: the root folder, always open, and the folders opened under it,
// each read when it is opened. Which folders are open lasts for the session. Main thread.
class FolderTree {
public:
    explicit FolderTree(std::filesystem::path root);

    const std::filesystem::path& root() const noexcept { return root_; }
    // The rows shown, top to bottom: a folder's entries follow it while it is open, folders
    // first, then files, each by name regardless of case. Every entry is listed.
    const std::vector<TreeRow>& rows() const noexcept { return rows_; }
    std::size_t selected() const noexcept { return selected_; }
    const TreeRow* selected_row() const;
    void select(std::size_t index);
    // Moves the selection by `delta` rows, never past either end.
    void move(long delta);

    // Right: opens the selected folder, reading it now.
    void expand();
    // Left: closes the selected folder if open; otherwise selects the folder holding it.
    // The root stays open.
    void collapse();
    // Enter on a folder: opens or closes it.
    void toggle();

    // Why the last folder could not be opened; empty when it could.
    const std::string& message() const noexcept { return message_; }

private:
    void rebuild();
    void add_children(const std::filesystem::path& dir, int depth);

    std::filesystem::path root_;
    std::set<std::filesystem::path> open_;  // folders open this session, the root among them
    std::vector<TreeRow> rows_;
    std::size_t selected_ = 0;
    std::string message_;
};

}  // namespace mod
