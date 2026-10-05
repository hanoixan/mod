#include "platform/file_map.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <format>

namespace mod {

Result<std::shared_ptr<const MappedFile>> MappedFile::open(const std::filesystem::path& path) {
    int fd;
    do {
        fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
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
    if (data_ != nullptr) ::munmap(const_cast<std::byte*>(data_), static_cast<std::size_t>(size_));
    if (fd_ >= 0) ::close(fd_);
}

}  // namespace mod
