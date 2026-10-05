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

// $XDG_CONFIG_HOME/mod, else $HOME/.config/mod. Not created.
Result<std::filesystem::path> user_config_dir();

}  // namespace mod
