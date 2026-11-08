#include "platform/file_map.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <format>

#if defined(__CYGWIN__)
// windows.h first: sys/cygwin.h declares its HANDLE functions only after it.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sys/cygwin.h>
#endif

namespace mod {
namespace {

// The file opened for reading. On Windows it is opened through the Windows API sharing read,
// write and delete, so another program can rewrite, truncate, rename over or delete it while
// mod reads it (the MSYS2 runtime's own open does not promise that), then given a descriptor.
int open_for_reading(const std::filesystem::path& path) {
#if defined(__CYGWIN__)
    const ssize_t n = ::cygwin_conv_path(CCP_POSIX_TO_WIN_W | CCP_ABSOLUTE, path.c_str(), nullptr, 0);
    if (n <= 0) return -1;
    std::wstring win(static_cast<std::size_t>(n) / sizeof(wchar_t), L'\0');
    if (::cygwin_conv_path(CCP_POSIX_TO_WIN_W | CCP_ABSOLUTE, path.c_str(), win.data(), static_cast<std::size_t>(n)) != 0) return -1;
    HANDLE h = ::CreateFileW(win.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        // A folder, a device or a missing file: the runtime's open says which, with errno set.
        int fd;
        do {
            fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        } while (fd < 0 && errno == EINTR);
        return fd;
    }
    std::string name = path.string();  // the runtime's name for the descriptor
    const int fd = ::cygwin_attach_handle_to_fd(name.data(), -1, h, 1, GENERIC_READ);
    if (fd < 0) ::CloseHandle(h);
    return fd;
#else
    int fd;
    do {
        fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    return fd;
#endif
}

FileLoad resolve(FileLoad load) {
    if (load != FileLoad::automatic) return load;
#if defined(__CYGWIN__)
    return FileLoad::read_in;
#else
    return FileLoad::map;
#endif
}

}  // namespace

bool maps_files() noexcept { return resolve(FileLoad::automatic) == FileLoad::map; }

Result<std::shared_ptr<const MappedFile>> MappedFile::open(const std::filesystem::path& path, FileLoad load) {
    load = resolve(load);
    const int fd = open_for_reading(path);
    if (fd < 0) {
        if (errno == ENXIO || errno == EISDIR) return std::unexpected(make_error(ErrorCode::unsupported, "not a regular file: " + path.string()));
        return std::unexpected(from_errno("open " + path.string()));
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        Error e = from_errno("stat " + path.string());
        ::close(fd);
        return std::unexpected(std::move(e));
    }
    if (!S_ISREG(st.st_mode)) {
        ::close(fd);
        return std::unexpected(make_error(ErrorCode::unsupported, "not a regular file: " + path.string()));
    }
    FileIdentity id{
        static_cast<std::uint64_t>(st.st_size),
#if defined(__APPLE__)
        static_cast<std::int64_t>(st.st_mtimespec.tv_sec) * 1'000'000'000 + st.st_mtimespec.tv_nsec,
#else
        static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1'000'000'000 + st.st_mtim.tv_nsec,
#endif
        static_cast<std::uint64_t>(st.st_dev),
        static_cast<std::uint64_t>(st.st_ino),
    };
    const auto size = static_cast<std::uint64_t>(st.st_size);
    if (load == FileLoad::read_in) {
        // Read whole, then closed: nothing of the file stays held. A file that grows while it
        // is read is read to the size stat gave; one that shrinks, to where it ended.
        std::unique_ptr<std::byte[]> bytes;
        if (size > 0) {
            try {
                bytes = std::make_unique_for_overwrite<std::byte[]>(static_cast<std::size_t>(size));
            } catch (const std::bad_alloc&) {
                ::close(fd);
                return std::unexpected(make_error(ErrorCode::too_large, std::format("cannot read {} ({} bytes) into memory", path.string(), size)));
            }
        }
        std::uint64_t got = 0;
        while (got < size) {
            const ssize_t r = ::read(fd, bytes.get() + got, static_cast<std::size_t>(std::min<std::uint64_t>(size - got, std::uint64_t{1} << 30)));
            if (r < 0 && errno == EINTR) continue;
            if (r < 0) {
                Error e = from_errno("read " + path.string());
                ::close(fd);
                return std::unexpected(std::move(e));
            }
            if (r == 0) break;
            got += static_cast<std::uint64_t>(r);
        }
        ::close(fd);
        id.size = got;
        return std::shared_ptr<const MappedFile>(new MappedFile(std::move(bytes), got, id));
    }
    const std::byte* data = nullptr;
    if (size > 0) {  // never mmap with length 0
        void* p = ::mmap(nullptr, static_cast<std::size_t>(size), PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) {
            const int e = errno;
            ::close(fd);
            if (e == ENOMEM) {
                struct rlimit rl {};
                std::string limit = "unlimited";
                if (::getrlimit(RLIMIT_AS, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
                    limit = std::format("{} bytes", static_cast<unsigned long long>(rl.rlim_cur));
                return std::unexpected(Error{ErrorCode::too_large, e,
                                             std::format("cannot map {} ({} bytes): address-space limit (RLIMIT_AS) is {}",
                                                         path.string(), size, limit)});
            }
            if (e == ENODEV || e == EINVAL || e == EACCES)
                return std::unexpected(Error{ErrorCode::unsupported, e, "file cannot be memory-mapped: " + path.string()});
            errno = e;
            return std::unexpected(from_errno("mmap " + path.string()));
        }
        data = static_cast<const std::byte*>(p);
#if defined(POSIX_MADV_SEQUENTIAL)
        ::posix_madvise(p, static_cast<std::size_t>(size), POSIX_MADV_SEQUENTIAL);
#endif
    }
    return std::shared_ptr<const MappedFile>(new MappedFile(fd, data, size, id));
}

MappedFile::~MappedFile() {
    // Read in, the bytes are owned_'s and freed with it; only a mapping is unmapped.
    if (data_ != nullptr && !owned_) ::munmap(const_cast<std::byte*>(data_), static_cast<std::size_t>(size_));
    if (fd_ >= 0) ::close(fd_);
}

}  // namespace mod
