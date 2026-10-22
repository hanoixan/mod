#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>

#include "platform/file_map.hpp"
#include "util/error.hpp"

namespace mod {

// Receives the next slice of content being written.
using ByteSink = std::function<Status(std::span<const std::byte>)>;
// Streams the whole content into the sink it is given.
using ContentProducer = std::function<Status(const ByteSink&)>;

// `not_found` when the path does not exist.
Result<FileIdentity> stat_path(const std::filesystem::path& path);

// Whether this process may write the existing file `path` (false when it is missing).
bool is_writable(const std::filesystem::path& path);

// Absolute path with symlinks resolved; the final component may not exist yet.
Result<std::filesystem::path> resolve_real_path(const std::filesystem::path& path);

// Crash-safe replacement of `target` through a temp file in its directory and a rename.
// Returns `not_atomic` (target untouched) when a rename would lose something or cannot
// happen; never falls back to an in-place write by itself. `mode`, when given, overrides
// only the permission bits; owner, attribute and ACL copying still follow `mode_from`.
Status write_atomically(const std::filesystem::path& target, const ContentProducer& produce,
                        const std::optional<std::filesystem::path>& mode_from = std::nullopt,
                        std::optional<std::uint32_t> mode = std::nullopt);

// Not crash-safe. Stages the content under $TMPDIR, then overwrites `target` in place,
// keeping its inode, links, owner, attributes and ACLs. Only after the user chose it.
Status write_in_place(const std::filesystem::path& target, const ContentProducer& produce);

// The user's home folder: on Windows %USERPROFILE% in its POSIX form (the MSYS2 runtime's
// $HOME is a folder of its own, missing when mod was not started from an MSYS2 shell), else
// $HOME (also the fallback on Windows). `not_found` when neither is set.
Result<std::filesystem::path> user_home_dir();

// $XDG_CONFIG_HOME/mod, else <user_home_dir>/.config/mod. Not created.
Result<std::filesystem::path> user_config_dir();

}  // namespace mod
