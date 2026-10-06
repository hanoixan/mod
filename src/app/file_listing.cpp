#include "app/file_listing.hpp"

#include <fnmatch.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <format>

#include "platform/path_text.hpp"

namespace fs = std::filesystem;

namespace mod {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string format_time(fs::file_time_type t) {
    // file_clock's own exact conversion: libc++ has no clock_cast, and an offset between two
    // now() readings is off by the time between them (a file at 00:00 showed 23:59).
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(fs::file_time_type::clock::to_sys(t));
    const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
    if (localtime_r(&tt, &tm) == nullptr) return {};
    char buf[32];
    if (std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm) == 0) return {};  // buf is unspecified then
    return buf;
}

Error from_ec(const std::error_code& ec, const std::string& context) {
    ErrorCode code = ErrorCode::io;
    if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory) code = ErrorCode::not_found;
    if (ec == std::errc::permission_denied || ec == std::errc::read_only_file_system) code = ErrorCode::permission;
    return Error{code, ec.value(), context + ": " + ec.message()};
}

}  // namespace

Result<std::vector<DirEntry>> list_directory(const fs::path& dir, const std::string& filter, bool show_hidden) {
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) return std::unexpected(from_ec(ec, dir.string()));
    const bool all = filter.empty() || filter == "*.*";
    std::vector<DirEntry> out;
    for (const fs::directory_entry& e : it) {
        const std::string name = e.path().filename().string();
        if (!show_hidden && !name.empty() && name[0] == '.') continue;
        std::error_code sec;
        const bool is_dir = fs::is_directory(e.path(), sec);  // follows links
        const auto status = fs::status(e.path(), sec);
        if (sec || status.type() == fs::file_type::not_found) continue;  // unreadable or a broken link
        if (!is_dir && !all && ::fnmatch(filter.c_str(), name.c_str(), FNM_CASEFOLD) != 0) continue;
        DirEntry d;
        d.name = name;
        d.is_dir = is_dir;
        if (!is_dir) {
            const auto size = fs::file_size(e.path(), sec);
            d.size = sec ? 0 : size;
        }
        const auto mtime = fs::last_write_time(e.path(), sec);
        d.modified = sec ? std::string() : format_time(mtime);
        out.push_back(std::move(d));
    }
    return out;
}

void sort_entries(std::vector<DirEntry>& entries, SortColumn column, bool ascending) {
    auto less = [&](const DirEntry& a, const DirEntry& b) {
        switch (column) {
            case SortColumn::size: return a.size < b.size;
            case SortColumn::modified: return a.modified < b.modified;
            default: return lower(a.name) < lower(b.name);
        }
    };
    const auto mid = std::stable_partition(entries.begin(), entries.end(), [](const DirEntry& e) { return e.is_dir; });
    std::stable_sort(entries.begin(), mid, less);  // directories: always ascending
    if (ascending) {
        std::stable_sort(mid, entries.end(), less);
    } else {
        std::stable_sort(mid, entries.end(), [&](const DirEntry& a, const DirEntry& b) { return less(b, a); });
    }
}

std::string format_size(std::uint64_t bytes) {
    constexpr double kKi = 1024.0;
    if (bytes < 1024) return std::format("{} B", bytes);
    const double b = static_cast<double>(bytes);
    if (bytes < 1024ULL * 1024) return std::format("{:.1f} KB", b / kKi);
    if (bytes < 1024ULL * 1024 * 1024) return std::format("{:.1f} MB", b / (kKi * kKi));
    return std::format("{:.1f} GB", b / (kKi * kKi * kKi));
}

std::vector<std::string> path_parts(const fs::path& dir) {
    const fs::path abs = fs::absolute(dir).lexically_normal();
    const std::string shown = display_path(abs);
    if (!shown.starts_with('/')) {
        // Windows: C:\Users\me reads C: > Users > me.
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (start <= shown.size()) {
            const std::size_t end = std::min(shown.find('\\', start), shown.size());
            if (end > start) parts.push_back(shown.substr(start, end - start));
            start = end + 1;
        }
        return parts;
    }
    std::vector<std::string> parts{"/"};
    for (const fs::path& p : abs.relative_path())
        if (!p.empty() && p != ".") parts.push_back(p.string());
    return parts;
}

Status make_directory(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) return std::unexpected(from_ec(ec, path.string()));
    return {};
}

}  // namespace mod
