#include "platform/fs.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/acl.h>
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace mod {
namespace {

FileIdentity identity_of(const struct stat& st) {
    return FileIdentity{
        static_cast<std::uint64_t>(st.st_size),
#if defined(__APPLE__)
        static_cast<std::int64_t>(st.st_mtimespec.tv_sec) * 1'000'000'000 + st.st_mtimespec.tv_nsec,
#else
        static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1'000'000'000 + st.st_mtim.tv_nsec,
#endif
        static_cast<std::uint64_t>(st.st_dev),
        static_cast<std::uint64_t>(st.st_ino),
    };
}

// Closes an fd and optionally unlinks a path when it goes out of scope.
struct TempGuard {
    int fd = -1;
    std::string path;
    bool keep = false;

    TempGuard() = default;
    TempGuard(int f, std::string p) : fd(f), path(std::move(p)) {}
    TempGuard(TempGuard&& o) noexcept : fd(std::exchange(o.fd, -1)), path(std::move(o.path)), keep(o.keep) {
        o.path.clear();
    }
    TempGuard& operator=(TempGuard&&) = delete;
    ~TempGuard() {
        if (fd >= 0) ::close(fd);
        if (!keep && !path.empty()) ::unlink(path.c_str());
    }
};

std::string random_suffix() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    return std::format("{:012x}", rng() & 0xFFFF'FFFF'FFFFull);
}

// Creates `<dir>/<prefix><random>` with O_EXCL and mode 0600.
Result<TempGuard> create_unique(const fs::path& dir, const std::string& prefix, int extra_flags) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string path = (dir / (prefix + random_suffix())).string();
        const int fd = ::open(path.c_str(), extra_flags | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0) return TempGuard(fd, std::move(path));
        if (errno == EEXIST || errno == EINTR) continue;
        return std::unexpected(from_errno("create temporary file in " + dir.string()));
    }
    return std::unexpected(make_error(ErrorCode::io, "cannot create a unique temporary file in " + dir.string()));
}

Status write_all(int fd, std::span<const std::byte> bytes, const std::string& what) {
    while (!bytes.empty()) {
        const ssize_t n = ::write(fd, bytes.data(), bytes.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(from_errno("write " + what));
        }
        bytes = bytes.subspan(static_cast<std::size_t>(n));
    }
    return {};
}

Status full_sync(int fd, const std::string& what) {
#if defined(F_FULLFSYNC)
    if (::fcntl(fd, F_FULLFSYNC) == 0) return {};  // macOS: also flush the drive cache
#endif
    if (::fsync(fd) != 0) return std::unexpected(from_errno("fsync " + what));
    return {};
}

// Streams `produce` into `fd`. A producer error is passed through unchanged.
Status stream_into(int fd, const ContentProducer& produce, const std::string& what) {
    const ByteSink sink = [&](std::span<const std::byte> bytes) { return write_all(fd, bytes, what); };
    return produce(sink);
}

mode_t current_umask() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("Umask:", 0) == 0) return static_cast<mode_t>(std::strtoul(line.c_str() + 6, nullptr, 8));
    }
#endif
    const mode_t m = ::umask(022);  // fallback: read by setting and restoring
    ::umask(m);
    return m;
}

bool in_group(gid_t gid) {
    if (gid == ::getegid()) return true;
    const int n = ::getgroups(0, nullptr);
    if (n <= 0) return false;
    std::vector<gid_t> groups(static_cast<std::size_t>(n));
    const int got = ::getgroups(n, groups.data());
    return got > 0 && std::find(groups.begin(), groups.begin() + got, gid) != groups.begin() + got;
}

std::vector<std::string> split_names(const std::vector<char>& buf, std::size_t len) {
    std::vector<std::string> names;
    std::size_t start = 0;
    for (std::size_t i = 0; i < len; ++i) {
        if (buf[i] == '\0') {
            if (i > start) names.emplace_back(buf.data() + start, i - start);
            start = i + 1;
        }
    }
    return names;
}

// Copies every extended attribute (and on Linux, through them, the POSIX ACLs) of
// `from` onto `to_fd`. Returns the failing attribute's name in the error message.
Status copy_xattrs(const fs::path& from, int to_fd) {
    std::vector<char> list(1024);
    ssize_t len;
    for (;;) {
#if defined(__APPLE__)
        len = ::listxattr(from.c_str(), list.data(), list.size(), 0);
#else
        len = ::listxattr(from.c_str(), list.data(), list.size());
#endif
        if (len >= 0) break;
        if (errno == ERANGE) {
            list.resize(list.size() * 4);
            continue;
        }
        if (errno == ENOTSUP) return {};  // filesystem without attributes: nothing to lose
        return std::unexpected(Error{ErrorCode::not_atomic, errno, "cannot list the file's extended attributes"});
    }
    for (const std::string& name : split_names(list, static_cast<std::size_t>(len))) {
        std::vector<char> value(256);
        ssize_t vlen;
        for (;;) {
#if defined(__APPLE__)
            vlen = ::getxattr(from.c_str(), name.c_str(), value.data(), value.size(), 0, 0);
#else
            vlen = ::getxattr(from.c_str(), name.c_str(), value.data(), value.size());
#endif
            if (vlen >= 0) break;
            if (errno == ERANGE) {
                value.resize(value.size() * 4);
                continue;
            }
#if defined(ENOATTR)
            if (errno == ENOATTR) break;  // removed meanwhile
#endif
            if (errno == ENODATA) break;
            return std::unexpected(Error{ErrorCode::not_atomic, errno, "cannot read extended attribute " + name});
        }
        if (vlen < 0) continue;
#if defined(__APPLE__)
        const int rc = ::fsetxattr(to_fd, name.c_str(), value.data(), static_cast<std::size_t>(vlen), 0, 0);
#else
        const int rc = ::fsetxattr(to_fd, name.c_str(), value.data(), static_cast<std::size_t>(vlen), 0);
#endif
        if (rc != 0)
            return std::unexpected(Error{ErrorCode::not_atomic, errno, "cannot copy extended attribute " + name});
    }
#if defined(__APPLE__)
    if (acl_t acl = ::acl_get_file(from.c_str(), ACL_TYPE_EXTENDED)) {
        const int rc = ::acl_set_fd(to_fd, acl);
        const int e = errno;
        ::acl_free(acl);
        if (rc != 0) return std::unexpected(Error{ErrorCode::not_atomic, e, "cannot copy the file's ACL"});
    }
#endif
    return {};
}

Result<fs::path> resolve_impl(const fs::path& path, int depth) {
    if (depth > 40) return std::unexpected(make_error(ErrorCode::io, "too many levels of symbolic links: " + path.string()));
    std::error_code ec;
    fs::path abs = fs::absolute(path, ec);
    if (ec) return std::unexpected(Error{ErrorCode::io, ec.value(), "cannot make path absolute: " + path.string()});
    if (char* real = ::realpath(abs.c_str(), nullptr)) {
        fs::path out(real);
        std::free(real);
        return out;
    }
    if (errno != ENOENT) return std::unexpected(from_errno("resolve " + abs.string()));
    // The final component may not exist yet; it may also be a dangling symlink.
    struct stat lst {};
    if (::lstat(abs.c_str(), &lst) == 0 && S_ISLNK(lst.st_mode)) {
        std::array<char, PATH_MAX> buf{};
        const ssize_t n = ::readlink(abs.c_str(), buf.data(), buf.size() - 1);
        if (n < 0) return std::unexpected(from_errno("readlink " + abs.string()));
        fs::path link_target(std::string(buf.data(), static_cast<std::size_t>(n)));
        if (link_target.is_relative()) link_target = abs.parent_path() / link_target;
        return resolve_impl(link_target, depth + 1);
    }
    const fs::path parent = abs.parent_path();
    char* real_parent = ::realpath(parent.c_str(), nullptr);
    if (real_parent == nullptr) return std::unexpected(from_errno("resolve " + parent.string()));
    fs::path out = fs::path(real_parent) / abs.filename();
    std::free(real_parent);
    return out;
}

}  // namespace

Result<FileIdentity> stat_path(const fs::path& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return std::unexpected(from_errno("stat " + path.string()));
    return identity_of(st);
}

Result<fs::path> resolve_real_path(const fs::path& path) { return resolve_impl(path, 0); }

bool is_writable(const fs::path& path) { return ::access(path.c_str(), W_OK) == 0; }

Status write_atomically(const fs::path& target, const ContentProducer& produce,
                        const std::optional<fs::path>& mode_from, std::optional<std::uint32_t> explicit_mode) {
    const fs::path dir = target.has_parent_path() ? target.parent_path() : fs::path(".");
    const std::string name = target.filename().string();

    struct stat tst {};
    const bool exists = ::stat(target.c_str(), &tst) == 0;
    if (!exists && errno != ENOENT) return std::unexpected(from_errno("stat " + target.string()));

    // Permission bits come from `mode_from`; owner, attributes and ACLs from the
    // target, and only when `mode_from` names an existing file.
    struct stat mst {};
    const bool copy_meta = mode_from.has_value() && ::stat(mode_from->c_str(), &mst) == 0;
    const mode_t mode = explicit_mode ? static_cast<mode_t>(*explicit_mode & 07777)
                        : copy_meta ? (mst.st_mode & 07777)
                                    : (0666 & ~current_umask());
    const bool want_owner = copy_meta && exists && (tst.st_uid != ::geteuid() || tst.st_gid != ::getegid());

    // Checks that run before anything is created.
    if (exists && tst.st_nlink > 1)
        return std::unexpected(make_error(ErrorCode::not_atomic, "file has other hard links"));
    if (want_owner) {
        const bool root = ::geteuid() == 0;
        if (!root && (tst.st_uid != ::geteuid() || !in_group(tst.st_gid)))
            return std::unexpected(make_error(ErrorCode::not_atomic, "file has a different owner or group"));
    }

    auto created = create_unique(dir, "." + name + ".mod-tmp-", O_WRONLY);
    if (!created) {
        Error e = std::move(created.error());
        if (exists && (e.sys_errno == EACCES || e.sys_errno == EPERM) && ::access(target.c_str(), W_OK) == 0)
            return std::unexpected(Error{ErrorCode::not_atomic, e.sys_errno, "directory not writable"});
        return std::unexpected(std::move(e));
    }
    TempGuard tmp = std::move(*created);

    if (auto s = stream_into(tmp.fd, produce, tmp.path); !s) return s;
    if (auto s = full_sync(tmp.fd, tmp.path); !s) return s;
    if (::fchmod(tmp.fd, mode) != 0) return std::unexpected(from_errno("chmod " + tmp.path));
    if (want_owner && ::fchown(tmp.fd, tst.st_uid, tst.st_gid) != 0)
        return std::unexpected(Error{ErrorCode::not_atomic, errno, "file has a different owner or group"});
    if (copy_meta && exists) {
        if (auto s = copy_xattrs(target, tmp.fd); !s) return s;  // after chmod: the ACL mask wins
    }
    if (::rename(tmp.path.c_str(), target.c_str()) != 0) return std::unexpected(from_errno("rename over " + target.string()));
    tmp.keep = true;  // it is the target now

    // Make the rename durable. A failure here cannot be undone, and the new content
    // is already in place, so it is not reported.
    const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
    return {};
}

Status write_in_place(const fs::path& target, const ContentProducer& produce) {
    struct stat tst {};
    if (::stat(target.c_str(), &tst) != 0) return std::unexpected(from_errno("stat " + target.string()));
    // Opened first, without truncating, so a target that cannot be written fails before staging.
    TempGuard out;
    out.fd = ::open(target.c_str(), O_WRONLY | O_CLOEXEC);
    if (out.fd < 0) return std::unexpected(from_errno("open " + target.string()));

    const char* env = std::getenv("TMPDIR");
    const fs::path stage_dir = (env != nullptr && *env != '\0') ? fs::path(env) : fs::path("/tmp");

    // Stage the whole new content first; `target` is untouched until this succeeds.
    auto created = create_unique(stage_dir, "mod-stage-", O_RDWR);
    if (!created) {
        Error e = std::move(created.error());
        e.message = "cannot stage the save in " + stage_dir.string() + ": " + e.message;
        return std::unexpected(std::move(e));
    }
    TempGuard stage = std::move(*created);  // removed on every path
    if (auto s = stream_into(stage.fd, produce, stage.path); !s) {
        Error e = std::move(s.error());
        e.message = "cannot stage the save in " + stage_dir.string() + ": " + e.message;
        return std::unexpected(std::move(e));
    }
    const off_t staged = ::lseek(stage.fd, 0, SEEK_CUR);
    if (staged < 0) return std::unexpected(from_errno("seek " + stage.path));

    // Then copy over the target.
    auto damaged = [&](Error e) {
        e.message = std::format("{}; the file on disk is damaged and should be saved again", e.message);
        return std::unexpected(std::move(e));
    };
    std::vector<std::byte> buf(1 << 20);
    off_t pos = 0;
    while (pos < staged) {
        const auto want = static_cast<std::size_t>(std::min<off_t>(staged - pos, static_cast<off_t>(buf.size())));
        const ssize_t n = ::pread(stage.fd, buf.data(), want, pos);
        if (n < 0) {
            if (errno == EINTR) continue;
            return damaged(from_errno("read " + stage.path));
        }
        if (n == 0) return damaged(make_error(ErrorCode::io, "stage file shrank: " + stage.path));
        std::size_t done = 0;
        while (done < static_cast<std::size_t>(n)) {
            const ssize_t w = ::pwrite(out.fd, buf.data() + done, static_cast<std::size_t>(n) - done, pos + static_cast<off_t>(done));
            if (w < 0) {
                if (errno == EINTR) continue;
                return damaged(from_errno("write " + target.string()));
            }
            done += static_cast<std::size_t>(w);
        }
        pos += n;
    }
    if (::ftruncate(out.fd, staged) != 0) return damaged(from_errno("truncate " + target.string()));
    if (auto s = full_sync(out.fd, target.string()); !s) return damaged(std::move(s.error()));
    return {};
}

Result<fs::path> user_config_dir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') return fs::path(xdg) / "mod";
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') return fs::path(home) / ".config" / "mod";
    return std::unexpected(make_error(ErrorCode::not_found, "neither XDG_CONFIG_HOME nor HOME is set"));
}

}  // namespace mod
