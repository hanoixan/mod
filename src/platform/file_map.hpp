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

// How a file's bytes are held: mapped (the file stays open and mapped while the MappedFile
// lives), or read into memory and the file closed at once.
enum class FileLoad { automatic, map, read_in };

// A read-only copy of a whole file, immutable after construction and readable from any
// thread: a mapping, or the file's bytes read into memory. A file truncated by another
// process while mapped raises SIGBUS on access; that risk is accepted (see the stand-in).
// `automatic` maps, except on Windows (the MSYS2 runtime): there a mapped or open file cannot
// be truncated or rewritten by another program, so the file is opened sharing read, write and
// delete, read in and closed, and nothing of it stays held.
// Whether `automatic` maps files here: then a file rewritten in place by another program shows
// its new bytes through the mapping, and a copy in memory cannot be kept.
bool maps_files() noexcept;

class MappedFile {
public:
    static Result<std::shared_ptr<const MappedFile>> open(const std::filesystem::path& path, FileLoad load = FileLoad::automatic);

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
    MappedFile(std::unique_ptr<std::byte[]> owned, std::uint64_t size, FileIdentity identity)
        : data_(owned.get()), size_(size), identity_(identity), owned_(std::move(owned)) {}

    int fd_ = -1;
    const std::byte* data_ = nullptr;
    std::uint64_t size_ = 0;
    FileIdentity identity_;
    std::unique_ptr<std::byte[]> owned_;  // read in: the bytes, and nothing mapped
};

}  // namespace mod
