#include "platform/path_text.hpp"

#include "platform/fs.hpp"

#if defined(__CYGWIN__)
#include <sys/cygwin.h>

#include <vector>
#endif

namespace mod {

#if defined(__CYGWIN__)
namespace {

// cygwin_conv_path into a string; `fallback` when it fails.
std::string convert(cygwin_conv_path_t how, const std::string& from, const std::string& fallback) {
    const ssize_t size = ::cygwin_conv_path(how | CCP_RELATIVE, from.c_str(), nullptr, 0);
    if (size <= 0) return fallback;
    std::vector<char> out(static_cast<std::size_t>(size));
    if (::cygwin_conv_path(how | CCP_RELATIVE, from.c_str(), out.data(), out.size()) != 0) return fallback;
    return std::string(out.data());
}

bool looks_like_windows(std::string_view t) {
    const bool drive = t.size() >= 2 && ((t[0] >= 'A' && t[0] <= 'Z') || (t[0] >= 'a' && t[0] <= 'z')) && t[1] == ':';
    return drive || t.starts_with("\\\\") || t.find('\\') != std::string_view::npos;
}

}  // namespace

std::string display_path(const std::filesystem::path& path) {
    if (path.empty()) return {};
    return convert(CCP_POSIX_TO_WIN_A, path.string(), path.string());
}

std::filesystem::path path_from_user(std::string_view text) {
    const std::string s = expand_home(text);
    if (!looks_like_windows(s)) return s;
    return convert(CCP_WIN_A_TO_POSIX, s, s);
}

std::string uri_path(const std::filesystem::path& path) {
    std::string s = display_path(path);
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    return s;
}

#else

std::string display_path(const std::filesystem::path& path) { return path.string(); }

std::filesystem::path path_from_user(std::string_view text) { return std::filesystem::path(std::string(text)); }

std::string uri_path(const std::filesystem::path& path) { return path.generic_string(); }

#endif

std::string expand_home(std::string_view text) {
#if defined(__CYGWIN__)
    constexpr std::string_view separators = "/\\";
#else
    constexpr std::string_view separators = "/";
#endif
    if (!text.starts_with('~') || (text.size() > 1 && separators.find(text[1]) == std::string_view::npos)) return std::string(text);
    const auto home = user_home_dir();
    if (!home) return std::string(text);
    return display_path(*home) + std::string(text.substr(1));
}

}  // namespace mod
