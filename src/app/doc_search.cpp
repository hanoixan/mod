#include "app/doc_search.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#ifndef MOD_INSTALL_DOC_DIR
#define MOD_INSTALL_DOC_DIR ""
#endif
#ifndef MOD_SOURCE_DOC_DIR
#define MOD_SOURCE_DOC_DIR ""
#endif

namespace mod {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxExcerpt = 200;

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::optional<fs::path> executable_path() {
    std::error_code ec;
#if defined(__APPLE__)
    char buf[4096];
    std::uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) return fs::weakly_canonical(fs::path(buf), ec);
    return std::nullopt;
#else
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return std::nullopt;
    return p;
#endif
}

}  // namespace

std::vector<fs::path> doc_pages(const fs::path& root) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".md") out.push_back(it->path().lexically_relative(root));
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        const bool ai = a == "index.md";
        const bool bi = b == "index.md";
        if (ai != bi) return ai;
        return a.generic_string() < b.generic_string();
    });
    return out;
}

DocSearchResult search_docs(const fs::path& root, std::string_view query, std::size_t cap) {
    DocSearchResult r;
    if (query.empty()) return r;
    const std::string needle = lower(query);
    for (const fs::path& page : doc_pages(root)) {
        std::ifstream in(root / page, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const std::string hay = lower(text);
        std::uint64_t line = 1;
        std::size_t line_start = 0;
        std::size_t counted = 0;  // offset up to which line feeds were counted
        for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + needle.size())) {
            for (; counted < at; ++counted) {
                if (text[counted] == '\n') {
                    ++line;
                    line_start = counted + 1;
                }
            }
            if (r.matches.size() == cap) {
                r.capped = true;
                return r;
            }
            std::size_t line_end = text.find('\n', at);
            if (line_end == std::string::npos) line_end = text.size();
            std::string_view excerpt = std::string_view(text).substr(line_start, line_end - line_start);
            while (!excerpt.empty() && (excerpt.front() == ' ' || excerpt.front() == '\t')) excerpt.remove_prefix(1);
            while (!excerpt.empty() && (excerpt.back() == ' ' || excerpt.back() == '\t' || excerpt.back() == '\r')) excerpt.remove_suffix(1);
            r.matches.push_back({page, at, line, at - line_start + 1, std::string(excerpt.substr(0, kMaxExcerpt))});
        }
    }
    return r;
}

DocDirSources doc_dir_sources() {
    DocDirSources s;
    if (const char* env = std::getenv("MOD_DOC_DIR"); env != nullptr && *env != '\0') s.env = fs::path(env);
    s.exe = executable_path();
    s.installed = MOD_INSTALL_DOC_DIR;
    s.source = MOD_SOURCE_DOC_DIR;
    return s;
}

std::vector<fs::path> doc_dir_candidates(const DocDirSources& s) {
    std::vector<fs::path> out;
    if (s.env) out.push_back(*s.env);
    if (s.exe) out.push_back(s.exe->parent_path().parent_path() / "share" / "mod" / "doc");
    if (!s.installed.empty()) out.push_back(s.installed);
    if (!s.source.empty()) out.push_back(s.source);
    return out;
}

std::optional<fs::path> find_doc_dir(const DocDirSources& s) {
    std::error_code ec;
    for (const fs::path& dir : doc_dir_candidates(s))
        if (fs::is_regular_file(dir / "index.md", ec)) return dir;
    return std::nullopt;
}

}  // namespace mod
