#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mod {

inline constexpr std::size_t kMaxDocMatches = 500;

// One match: the page (relative to the manual's root), where in it, and the line's text.
struct DocMatch {
    std::filesystem::path page;
    std::uint64_t offset = 0;  // bytes into the page
    std::uint64_t line = 0;    // 1-based
    std::uint64_t column = 0;  // 1-based, in bytes
    std::string excerpt;       // the matching line, trimmed
};

struct DocSearchResult {
    std::vector<DocMatch> matches;
    bool capped = false;  // more matches than the cap
};

// Every .md file under `root`, relative to it: index.md first, the rest in path order.
std::vector<std::filesystem::path> doc_pages(const std::filesystem::path& root);
// Plain-text search of every page, ignoring ASCII case, in page order.
DocSearchResult search_docs(const std::filesystem::path& root, std::string_view query, std::size_t cap = kMaxDocMatches);

// Where the manual may be, in the order they are tried.
struct DocDirSources {
    std::optional<std::filesystem::path> env;  // $MOD_DOC_DIR
    std::optional<std::filesystem::path> exe;  // the running binary, for <exe>/../share/mod/doc
    std::filesystem::path installed;           // the install path compiled in
    std::filesystem::path source;              // the source tree's docs/manual
};
// The real sources of this process.
DocDirSources doc_dir_sources();
// The folders that would be tried, in order.
std::vector<std::filesystem::path> doc_dir_candidates(const DocDirSources& sources);
// The first candidate that holds index.md.
std::optional<std::filesystem::path> find_doc_dir(const DocDirSources& sources);

}  // namespace mod
