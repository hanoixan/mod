#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace mod {

// Paths as the user reads and types them. On Windows, mod runs on the MSYS2 runtime and
// works with POSIX paths (/c/Users/me), but shows Windows ones (C:\Users\me) and accepts
// either; native Windows programs, such as language servers, are given Windows paths too.
// Elsewhere every function here returns the path as it is.

// The path as the user reads it, and as a native program on this system expects it.
std::string display_path(const std::filesystem::path& path);

// A path the user typed or passed on the command line: on Windows a leading `~` is
// expanded (`expand_home`; elsewhere the shell has done that), then `C:\…`, `C:/…` and
// `\\server\share\…` become their POSIX form; anything else is taken as it is.
std::filesystem::path path_from_user(std::string_view text);

// `~` alone or before a `/` (on Windows also a `\`) at the start of `text` replaced by
// the home folder as the user reads it (`user_home_dir`, through `display_path`). Anything
// else, `~user` included, and any text when there is no home folder, is returned as it is.
std::string expand_home(std::string_view text);

// The path part of a `file://` URI, before percent-encoding: the generic form, and on
// Windows the drive form with forward slashes (C:/Users/me), as native servers expect.
std::string uri_path(const std::filesystem::path& path);

}  // namespace mod
