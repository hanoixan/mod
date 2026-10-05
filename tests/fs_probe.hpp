#pragma once

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <system_error>

// What the file system under the tests does, for tests of behavior it may lack. On Windows
// (the MSYS2 runtime, mounted noacl by default) permissions are not kept or enforced, and
// symbolic links may be copies; as root, nothing is unreadable.
namespace mod::probe {

namespace detail {
inline std::filesystem::path scratch_file(const std::filesystem::path& dir, const char* name) {
    std::filesystem::create_directories(dir);
    const std::filesystem::path p = dir / name;
    std::filesystem::remove(p);
    std::ofstream(p) << "x";
    return p;
}
}  // namespace detail

// chmod 0640 reads back as 0640.
inline bool permissions_kept(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    const fs::path p = detail::scratch_file(dir, ".probe-kept");
    fs::permissions(p, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    std::error_code ec;
    const bool kept = (fs::status(p, ec).permissions() & fs::perms::mask) ==
                      (fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    fs::remove(p, ec);
    return kept;
}

// A file with no permissions cannot be opened (false as root, or where chmod is not enforced).
inline bool permissions_enforced(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    if (::geteuid() == 0) return false;
    const fs::path p = detail::scratch_file(dir, ".probe-enforced");
    fs::permissions(p, fs::perms::none);
    const bool enforced = !std::ifstream(p).is_open();
    fs::permissions(p, fs::perms::owner_all);
    std::error_code ec;
    fs::remove(p, ec);
    return enforced;
}

// A symbolic link to a file that does not exist yet is a real link.
inline bool symlinks_work(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    fs::create_directories(dir);
    const fs::path link = dir / ".probe-link";
    std::error_code ec;
    fs::remove(link, ec);
    fs::create_symlink(".probe-nowhere", link, ec);
    const bool ok = !ec && fs::is_symlink(fs::symlink_status(link, ec));
    fs::remove(link, ec);
    return ok;
}

}  // namespace mod::probe
