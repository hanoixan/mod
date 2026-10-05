#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "util/error.hpp"

namespace mod {

struct DirEntry {
    std::string name;
    bool is_dir = false;
    std::uint64_t size = 0;  // 0 for a directory
    std::string modified;    // local time, "YYYY-MM-DD HH:MM"
};

enum class SortColumn { name, size, modified };

// Unsorted. Directories are always listed; a file only when its name matches `filter`
// (a shell pattern, ignoring case; "*.*" or empty matches every file).
Result<std::vector<DirEntry>> list_directory(const std::filesystem::path& dir, const std::string& filter, bool show_hidden);

// Directories first and ascending whatever the direction, then the files.
void sort_entries(std::vector<DirEntry>& entries, SortColumn column, bool ascending);

// "123 B", "1.5 KB", "2.0 MB", "3.1 GB" (powers of 1024).
std::string format_size(std::uint64_t bytes);

// "/home/user" gives {"/", "home", "user"}.
std::vector<std::string> path_parts(const std::filesystem::path& dir);

// `mkdir -p`: an existing folder is not an error.
Status make_directory(const std::filesystem::path& path);

}  // namespace mod
