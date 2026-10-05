#include "syntax/language_config.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <system_error>

#include "platform/fs.hpp"
#include "util/log.hpp"

namespace mod {
namespace {

// Generated at configure time from config/languages.json: `kDefaultLanguagesJson`.
#include "default_languages.inc"

constexpr std::uintmax_t kMaxUserFileBytes = 1u << 20;

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

Error malformed(std::string what) { return make_error(ErrorCode::format, "languages.json: " + std::move(what)); }

Result<std::vector<std::string>> string_list(const Json* v, std::string_view field, std::string_view id) {
    if (v == nullptr || !v->is_array() || v->is_compact_ints() || v->size() == 0)
        return std::unexpected(malformed(std::format("\"{}\" of \"{}\" must be a non-empty array of strings", field, id)));
    std::vector<std::string> out;
    for (const Json& e : v->elements()) {
        if (!e.is_string() || e.as_string().empty())
            return std::unexpected(malformed(std::format("\"{}\" of \"{}\" must hold non-empty strings", field, id)));
        out.push_back(e.as_string());
    }
    return out;
}

// A list of non-empty strings, which may itself be empty.
Result<std::vector<std::string>> word_list(const Json& v, std::string_view field, std::string_view id) {
    if (!v.is_array() || (v.size() > 0 && v.is_compact_ints()))
        return std::unexpected(malformed(std::format("\"syntax.{}\" of \"{}\" must be an array of strings", field, id)));
    std::vector<std::string> out;
    for (const Json& e : v.elements()) {
        if (!e.is_string() || e.as_string().empty())
            return std::unexpected(malformed(std::format("\"syntax.{}\" of \"{}\" must hold non-empty strings", field, id)));
        out.push_back(e.as_string());
    }
    return out;
}

Result<bool> flag(const Json& v, std::string_view field, std::string_view id) {
    if (!v.is_bool()) return std::unexpected(malformed(std::format("\"syntax.{}\" of \"{}\" must be true or false", field, id)));
    return v.as_bool();
}

Result<StringRule> string_rule(const Json& v, std::string_view id) {
    auto bad = [&](std::string_view what) {
        return std::unexpected(malformed(std::format("a string of \"{}\" {}", id, what)));
    };
    if (!v.is_object()) return bad("must be an object");
    StringRule rule;
    const Json* open = v.get("open");
    if (open == nullptr || !open->is_string() || open->as_string().empty()) return bad("needs a non-empty \"open\"");
    rule.open = open->as_string();
    rule.close = rule.open;
    if (const Json* close = v.get("close")) {
        if (!close->is_string() || close->as_string().empty()) return bad("needs a non-empty \"close\"");
        rule.close = close->as_string();
    }
    if (const Json* escape = v.get("escape")) {
        if (!escape->is_string() || escape->as_string().size() != 1) return bad("needs a one-character \"escape\"");
        rule.escape = escape->as_string()[0];
    }
    if (const Json* multiline = v.get("multiline")) {
        if (!multiline->is_bool()) return bad("needs \"multiline\" true or false");
        rule.multiline = multiline->as_bool();
    }
    if (const Json* ch = v.get("charLiteral")) {
        if (!ch->is_bool()) return bad("needs \"charLiteral\" true or false");
        rule.char_literal = ch->as_bool();
    }
    if (const Json* max = v.get("maxLength")) {
        if (!max->is_int() || max->as_int() <= 0 || max->as_int() > 0xffffffffLL) return bad("needs a positive \"maxLength\"");
        rule.max_length = static_cast<std::uint32_t>(max->as_int());
    }
    return rule;
}

Result<SyntaxSpec> parse_syntax(const Json& v, std::string_view id) {
    if (!v.is_object()) return std::unexpected(malformed(std::format("\"syntax\" of \"{}\" must be an object", id)));
    SyntaxSpec spec;
    for (auto [field, list] : {std::pair{"keywords", &spec.keywords}, std::pair{"constants", &spec.constants},
                               std::pair{"lineComments", &spec.line_comments}, std::pair{"stringPrefixes", &spec.string_prefixes}}) {
        if (const Json* j = v.get(field)) {
            auto words = word_list(*j, field, id);
            if (!words) return std::unexpected(words.error());
            *list = std::move(*words);
        }
    }
    for (auto [field, value] : {std::pair{"caseSensitive", &spec.case_sensitive},
                                std::pair{"commentNeedsSpace", &spec.comment_needs_space}, std::pair{"numbers", &spec.numbers}}) {
        if (const Json* j = v.get(field)) {
            auto b = flag(*j, field, id);
            if (!b) return std::unexpected(b.error());
            *value = *b;
        }
    }
    if (const Json* blocks = v.get("blockComments")) {
        if (!blocks->is_array() || (blocks->size() > 0 && blocks->is_compact_ints()))
            return std::unexpected(malformed(std::format("\"syntax.blockComments\" of \"{}\" must be an array", id)));
        for (const Json& pair : blocks->elements()) {
            auto words = word_list(pair, "blockComments", id);
            if (!words || words->size() != 2)
                return std::unexpected(malformed(std::format("each block comment of \"{}\" must be [open, close]", id)));
            spec.block_comments.emplace_back(std::move((*words)[0]), std::move((*words)[1]));
        }
    }
    if (const Json* strings = v.get("strings")) {
        if (!strings->is_array() || (strings->size() > 0 && strings->is_compact_ints()))
            return std::unexpected(malformed(std::format("\"syntax.strings\" of \"{}\" must be an array", id)));
        for (const Json& s : strings->elements()) {
            auto rule = string_rule(s, id);
            if (!rule) return std::unexpected(rule.error());
            spec.strings.push_back(std::move(*rule));
        }
    }
    return spec;
}

std::vector<LanguageServerSpec> defaults() {
    auto parsed = LanguageConfig::parse(kDefaultLanguagesJson);
    if (!parsed) {
        // A build bug; json_test parses the embedded text so this never ships.
        log(LogLevel::error, "embedded languages.json: {}", parsed.error().message);
        return {};
    }
    return std::move(*parsed);
}

}  // namespace

std::string_view LanguageConfig::default_json() noexcept { return kDefaultLanguagesJson; }

Result<std::vector<LanguageServerSpec>> LanguageConfig::parse(std::string_view text) {
    auto doc = Json::parse(text);
    if (!doc) return std::unexpected(malformed(doc.error().message));
    if (!doc->is_object()) return std::unexpected(malformed("the top level must be an object"));
    const Json* version = doc->get("version");
    if (version == nullptr || !version->is_int() || version->as_int() != 1)
        return std::unexpected(malformed("\"version\" must be 1"));
    const Json* languages = doc->get("languages");
    if (languages == nullptr || !languages->is_array() || (languages->size() > 0 && languages->is_compact_ints()))
        return std::unexpected(malformed("\"languages\" must be an array"));

    std::vector<LanguageServerSpec> out;
    for (const Json& entry : languages->elements()) {
        if (!entry.is_object()) return std::unexpected(malformed("each language must be an object"));
        LanguageServerSpec spec;
        const Json* id = entry.get("id");
        if (id == nullptr || !id->is_string() || id->as_string().empty())
            return std::unexpected(malformed("each language needs a non-empty \"id\""));
        spec.id = id->as_string();
        if (std::ranges::any_of(out, [&](const LanguageServerSpec& s) { return s.id == spec.id; }))
            return std::unexpected(malformed(std::format("\"{}\" is listed twice", spec.id)));

        const Json* ext_list = entry.get("extensions");
        const Json* name_list = entry.get("fileNames");
        if (ext_list == nullptr && name_list == nullptr)
            return std::unexpected(malformed(std::format("\"{}\" needs \"extensions\" or \"fileNames\"", spec.id)));
        if (ext_list != nullptr) {
            auto exts = string_list(ext_list, "extensions", spec.id);
            if (!exts) return std::unexpected(exts.error());
            for (std::string& e : *exts) spec.extensions.push_back(lower(e.starts_with('.') ? std::move(e) : "." + e));
        }
        if (name_list != nullptr) {
            auto names = string_list(name_list, "fileNames", spec.id);
            if (!names) return std::unexpected(names.error());
            spec.file_names = std::move(*names);
        }

        const Json* command_list = entry.get("command");
        const Json* syntax = entry.get("syntax");
        if (command_list == nullptr && (syntax == nullptr || syntax->is_null()))
            return std::unexpected(malformed(std::format("\"{}\" needs \"command\" or \"syntax\"", spec.id)));
        if (command_list != nullptr) {
            auto command = string_list(command_list, "command", spec.id);
            if (!command) return std::unexpected(command.error());
            spec.command = std::move(*command);
        }
        spec.syntax_given = syntax != nullptr;
        if (syntax != nullptr && !syntax->is_null()) {
            auto parsed = parse_syntax(*syntax, spec.id);
            if (!parsed) return std::unexpected(parsed.error());
            spec.syntax = std::move(*parsed);
        }

        if (const Json* init = entry.get("initializationOptions")) spec.initialization_options = *init;

        if (const Json* max = entry.get("maxFileBytes")) {
            if (!max->is_int() || max->as_int() <= 0)
                return std::unexpected(
                    malformed(std::format("\"maxFileBytes\" of \"{}\" must be a positive integer", spec.id)));
            spec.max_file_bytes = static_cast<std::uint64_t>(max->as_int());
        }
        out.push_back(std::move(spec));
    }
    return out;
}

LanguageConfig::Loaded LanguageConfig::merge(std::optional<std::string_view> user_text, std::string_view user_name) {
    Loaded result;
    std::vector<LanguageServerSpec> base = defaults();
    if (user_text) {
        auto user = parse(*user_text);
        if (!user) {
            std::string msg = user.error().message;
            if (msg.starts_with("languages.json: ")) msg.erase(0, 16);
            result.warning = std::format("{}: {}; using the defaults", user_name, msg);
        } else {
            // User entries replace defaults with the same id and are searched first. One
            // that says nothing about syntax keeps the default's: it describes the
            // language, not the server the user chose.
            for (LanguageServerSpec& u : *user) {
                if (u.syntax_given) continue;
                const auto d = std::ranges::find(base, u.id, &LanguageServerSpec::id);
                if (d != base.end()) u.syntax = d->syntax;
            }
            std::erase_if(base, [&](const LanguageServerSpec& d) {
                return std::ranges::any_of(*user, [&](const LanguageServerSpec& u) { return u.id == d.id; });
            });
            result.config.specs_ = std::move(*user);
        }
    }
    for (auto& d : base) result.config.specs_.push_back(std::move(d));
    return result;
}

LanguageConfig::Loaded LanguageConfig::load(const std::optional<std::filesystem::path>& config_dir) {
    if (!config_dir) return merge(std::nullopt);
    const std::filesystem::path file = *config_dir / "languages.json";
    std::error_code ec;
    const auto status = std::filesystem::status(file, ec);
    if (ec || !std::filesystem::exists(status)) return merge(std::nullopt);  // a missing file is normal
    const std::string name = file.string();
    if (!std::filesystem::is_regular_file(status)) {
        Loaded r = merge(std::nullopt);
        r.warning = name + ": not a regular file; using the defaults";
        return r;
    }
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size > kMaxUserFileBytes) {
        Loaded r = merge(std::nullopt);
        r.warning = name + (ec ? ": cannot read it" : ": larger than 1 MiB") + "; using the defaults";
        return r;
    }
    std::ifstream in(file, std::ios::binary);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!in || !in.read(text.data(), static_cast<std::streamsize>(text.size()))) {
        Loaded r = merge(std::nullopt);
        r.warning = name + ": cannot read it; using the defaults";
        return r;
    }
    return merge(text, name);
}

LanguageConfig::Loaded LanguageConfig::load() {
    auto dir = user_config_dir();
    return load(dir ? std::optional<std::filesystem::path>(*dir) : std::nullopt);
}

const LanguageServerSpec* LanguageConfig::find_for_path(const std::filesystem::path& path) const {
    const std::string name = path.filename().string();
    for (const LanguageServerSpec& s : specs_)
        if (std::ranges::find(s.file_names, name) != s.file_names.end()) return &s;
    const std::string ext = lower(path.extension().string());
    if (ext.empty()) return nullptr;
    for (const LanguageServerSpec& s : specs_)
        if (std::ranges::find(s.extensions, ext) != s.extensions.end()) return &s;
    return nullptr;
}

std::filesystem::path find_project_root(const std::filesystem::path& file) {
    static constexpr std::string_view kMarkers[] = {".git", "compile_commands.json", "Cargo.toml", "go.mod", "package.json", "pyproject.toml"};
    std::filesystem::path start = std::filesystem::absolute(file).parent_path();
    std::error_code ec;
    for (std::filesystem::path dir = start;; dir = dir.parent_path()) {
        for (const std::string_view m : kMarkers)
            if (std::filesystem::exists(dir / m, ec)) return dir;
        if (dir == dir.parent_path()) break;  // the filesystem root
    }
    return start;
}

}  // namespace mod
