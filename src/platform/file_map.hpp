#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "util/error.hpp"

namespace mod {

// Equality means "probably unchanged"; ContentHash is the authoritative check.
struct FileIdentity {
    std::uint64_t size = 0;
    std::int64_t mtime_ns = 0;
    std::uint64_t device = 0;
    std::uint64_t inode = 0;

    friend bool operator==(const FileIdentity&, const FileIdentity&) = default;
};

// A read-only mapping of a whole file, immutable after construction and readable
// from any thread. A file truncated by another process while mapped raises SIGBUS
// on access; that risk is accepted (see the stand-in).
class MappedFile {
public:
    static Result<std::shared_ptr<const MappedFile>> open(const std::filesystem::path& path);

    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    // nullptr when the file is empty.
    const std::byte* data() const noexcept { return data_; }
    std::uint64_t size() const noexcept { return size_; }
    const FileIdentity& identity() const noexcept { return identity_; }

private:
    MappedFile(int fd, const std::byte* data, std::uint64_t size, FileIdentity identity)
        : fd_(fd), data_(data), size_(size), identity_(identity) {}

    int fd_ = -1;
    const std::byte* data_ = nullptr;
    std::uint64_t size_ = 0;
    FileIdentity identity_;
};

}  // namespace mod
