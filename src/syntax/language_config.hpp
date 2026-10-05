#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "syntax/json.hpp"
#include "util/error.hpp"

namespace mod {

inline constexpr std::uint64_t kDefaultLspMaxFileBytes = std::uint64_t{4} << 20;

// One kind of string in a SyntaxSpec.
struct StringRule {
    std::string open;
    std::string close;                       // the opening delimiter when not given
    std::optional<char> escape;              // the character that escapes the next one
    bool multiline = false;                  // may run on past the end of its line
    std::optional<std::uint32_t> max_length; // not a string unless closed within this many bytes
    bool char_literal = false;               // a string only around one character or one escape sequence
};

// The `syntax` member of a language entry: what the syntax layer colors.
struct SyntaxSpec {
    std::vector<std::string> keywords;
    std::vector<std::string> constants;
    bool case_sensitive = true;
    std::vector<std::string> line_comments;
    bool comment_needs_space = false;  // a line-comment marker counts only at a line's start or after a blank
    std::vector<std::pair<std::string, std::string>> block_comments;
    std::vector<StringRule> strings;
    std::vector<std::string> string_prefixes;
    bool numbers = true;
};

// A language: how to color it without a server (`syntax`), the server that adds
// colors for its names (`command`), or both.
struct LanguageServerSpec {
    std::string id;                       // the LSP languageId; unique
    std::vector<std::string> extensions;  // lowercase, with the leading dot
    std::vector<std::string> file_names;  // whole file names, matched exactly
    std::vector<std::string> command;     // empty when there is no server
    Json initialization_options;          // null when absent
    std::uint64_t max_file_bytes = kDefaultLspMaxFileBytes;
    std::optional<SyntaxSpec> syntax;
    bool syntax_given = false;  // the entry has a `syntax` member (null turns the layer off)
};

// Extension → language server, from the embedded defaults merged with the user's
// languages.json. Immutable after loading. Main thread.
class LanguageConfig {
public:
    struct Loaded;

    // Defaults plus `<user_config_dir>/languages.json` when it exists.
    static Loaded load();
    // Defaults plus `<config_dir>/languages.json`; no user file without a directory.
    static Loaded load(const std::optional<std::filesystem::path>& config_dir);
    // Defaults plus the given user file text (`nullopt`: none). A malformed user text
    // is ignored with a warning naming `user_name` and the byte offset.
    static Loaded merge(std::optional<std::string_view> user_text, std::string_view user_name = "languages.json");

    // Parses one languages.json document.
    static Result<std::vector<LanguageServerSpec>> parse(std::string_view text);
    // The defaults compiled in from config/languages.json.
    static std::string_view default_json() noexcept;

    // The spec whose extensions contain `path`'s lowercase extension, or null.
    const LanguageServerSpec* find_for_path(const std::filesystem::path& path) const;
    const std::vector<LanguageServerSpec>& specs() const noexcept { return specs_; }

private:
    std::vector<LanguageServerSpec> specs_;  // user entries first, then the defaults they do not override
};

struct LanguageConfig::Loaded {
    LanguageConfig config;
    std::optional<std::string> warning;
};

// The folder a language server should treat as the project: the nearest folder, from
// the file's own upwards, holding .git, compile_commands.json, Cargo.toml, go.mod,
// package.json or pyproject.toml; the file's own folder when none does.
std::filesystem::path find_project_root(const std::filesystem::path& file);

}  // namespace mod
