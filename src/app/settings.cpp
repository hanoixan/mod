#include "app/settings.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace mod {
namespace {

constexpr std::uintmax_t kMaxSettingsBytes = 1u << 20;
constexpr std::string_view kModifiedKey = "modified";

using T = SettingType;

// The schema. Adding a row is all it takes for a setting to be loaded, saved, listed in
// the User Settings panel and offered among the recent settings; App then acts on it.
constexpr std::string_view kTerminalModes[] = {"auto", "vt100", "xterm"};
constexpr std::string_view kDarkness[] = {"night", "normal", "paper"};
constexpr std::string_view kTabInserts[] = {"spaces", "tab"};
constexpr std::string_view kReadOnlyCopy[] = {"markdown", "visible text"};
constexpr std::string_view kCursorStyles[] = {"bar", "bar-blink", "block", "block-blink", "underline", "underline-blink"};

constexpr SettingSpec kSpecs[] = {
    {"tab_width", T::integer, kDefaultTabWidth, kMinTabWidth, kMaxTabWidth, "Tab width",
     "Columns between tab stops, 1 to 16: how wide a tab character looks, and how far Tab and Shift+Tab indent "
     "with spaces. It never changes the file by itself.", {}},
    {"line_numbers", T::boolean, 1, 0, 1, "Line numbers on open",
     "Show the line-number gutter in each document as it is opened; changing it also sets every open document. "
     "View > Line Numbers changes it for one document.", {}},
    {"syntax_coloring", T::boolean, 1, 0, 1, "Syntax coloring on open",
     "Color keywords, strings, comments and numbers, a language server's names, and Markdown, in each document as "
     "it is opened; changing it also sets every open document. View > Syntax Coloring changes it for one document.", {}},
    {"word_wrap", T::boolean, 1, 0, 1, "Word wrap on open",
     "Wrap long lines onto the next rows in each document as it is opened, instead of scrolling sideways; "
     "changing it also sets every open document. View > Word Wrap changes it for one document.", {}},
    {"pin_folder_tree", T::boolean, 0, 0, 1, "Pin Folder Tree",
     "Keep the folder tree, the working folder's files and folders, shown to the left of the views. Unpinned, it shows "
     "only while you are in it (Esc, then Shift+Left) and goes when you leave. Changing it also pins or unpins it now; "
     "View > Pin Folder Tree changes it for this session.", {}},
    {"terminal_mode", T::choice, 0, 0, 2, "Terminal mode",
     "How mod writes to the terminal: auto detects it, vt100 uses only VT100 codes (bold, underline and reverse, no "
     "color), xterm uses colors and modern features. Applies the next time mod starts.", kTerminalModes},
    {"darkness", T::choice, 1, 0, 2, "Darkness",
     "How dark the screen is. normal uses reverse video for the bars and markers; night draws them as bold "
     "bright text on a dark grey band; paper draws the text on a light page and the bars as plain text.", kDarkness},
    {"cursor_style", T::choice, 0, 0, 5, "Cursor style",
     "The text cursor's shape: a vertical bar, a block or an underline, steady or blinking. Some terminals "
     "ignore it, and it has no effect in vt100 mode.", kCursorStyles},
    {"read_only_copy", T::choice, 0, 0, 1, "Copy in read-only Markdown",
     "What Copy takes from a Markdown document shown laid out in read-only mode: markdown copies the source, marks "
     "and all; visible text copies the text as it is shown.", kReadOnlyCopy},
    {"tab_inserts", T::choice, 0, 0, 1, "Tab inserts",
     "What the Tab key inserts: spaces up to the next tab stop (the tab width apart), or a tab character. "
     "Shift+Tab removes up to a tab width of leading spaces, or a leading tab.", kTabInserts},
    {"keymap", T::keymap, 0, 0, 0, "Key bindings",
     "The keys for every command. Enter opens the Key Bindings editor, which is also at Options > Key Bindings…. "
     "Only the commands you changed are stored.", {}},
    {"colors", T::colors, 0, 0, 0, "Colors",
     "How each kind of text and each part of the screen looks. Enter opens the Colors editor, which is also at "
     "Options > Colors…. Only the colors you changed are stored.", {}},
};

// A structured value, read and written whole by its owner through `raw` and `set_raw`.
bool structured(T type) { return type == T::keymap || type == T::colors; }

std::optional<std::size_t> index_of(std::string_view key) {
    for (std::size_t i = 0; i < std::size(kSpecs); ++i) {
        if (kSpecs[i].key == key) return i;
    }
    return std::nullopt;
}

std::string expectation(const SettingSpec& s) {
    if (s.type == T::boolean) return std::format("{} must be true or false", s.key);
    if (s.type == T::choice) {
        std::string names;
        for (std::string_view c : s.choices) names += (names.empty() ? "" : ", ") + std::string(c);
        return std::format("{} must be one of {}", s.key, names);
    }
    return std::format("{} must be a whole number from {} to {}", s.key, s.min, s.max);
}

// The value of `j` for `spec`, or nullopt when it has the wrong type or is out of range.
std::optional<std::int64_t> read_value(const SettingSpec& spec, const Json& j) {
    if (spec.type == T::boolean) {
        if (!j.is_bool()) return std::nullopt;
        return j.as_bool() ? 1 : 0;
    }
    if (spec.type == T::choice) {
        if (!j.is_string()) return std::nullopt;
        const auto it = std::ranges::find(spec.choices, std::string_view(j.as_string()));
        if (it == spec.choices.end()) return std::nullopt;
        return static_cast<std::int64_t>(it - spec.choices.begin());
    }
    if (!j.is_int() || j.as_int() < spec.min || j.as_int() > spec.max) return std::nullopt;
    return j.as_int();
}

bool digits_at(std::string_view s, std::size_t pos, std::size_t n) {
    for (std::size_t i = pos; i < pos + n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

// "YYYY-MM-DDTHH:MM:SS.mmmZ", the form that sorts as text; the form without
// milliseconds is accepted and widened. Empty for anything else.
std::string normalize_time(std::string_view s) {
    if (s.size() != 20 && s.size() != 24) return {};
    if (!digits_at(s, 0, 4) || s[4] != '-' || !digits_at(s, 5, 2) || s[7] != '-' || !digits_at(s, 8, 2) || s[10] != 'T' ||
        !digits_at(s, 11, 2) || s[13] != ':' || !digits_at(s, 14, 2) || s[16] != ':' || !digits_at(s, 17, 2) || s.back() != 'Z')
        return {};
    if (s.size() == 20) return std::string(s.substr(0, 19)) + ".000Z";
    if (s[19] != '.' || !digits_at(s, 20, 3)) return {};
    return std::string(s);
}

std::string format_time(std::int64_t unix_ms) {
    const std::int64_t ms = ((unix_ms % 1000) + 1000) % 1000;
    const std::time_t secs = static_cast<std::time_t>((unix_ms - ms) / 1000);
    std::tm tm{};
    (void)::gmtime_r(&secs, &tm);  // fails only past year 2^31; tm then stays zeroed
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                       tm.tm_min, tm.tm_sec, ms);
}

}  // namespace

std::span<const SettingSpec> setting_specs() { return kSpecs; }

const SettingSpec* find_setting(std::string_view key) {
    const auto i = index_of(key);
    return i ? &kSpecs[*i] : nullptr;
}

std::string setting_value_text(const SettingSpec& spec, std::int64_t value) {
    if (structured(spec.type)) return {};
    if (spec.type == T::boolean) return value != 0 ? "on" : "off";
    if (spec.type == T::choice)
        return value >= 0 && static_cast<std::size_t>(value) < spec.choices.size() ? std::string(spec.choices[static_cast<std::size_t>(value)]) : std::string();
    return std::to_string(value);
}

std::string default_settings_json() {
    std::string out = "{\n";
    for (const SettingSpec& s : kSpecs) {
        if (structured(s.type)) continue;
        const std::string value = s.type == T::boolean ? (s.def != 0 ? "true" : "false")
                                  : s.type == T::choice ? std::format("\"{}\"", s.choices[static_cast<std::size_t>(s.def)])
                                                        : std::to_string(s.def);
        out += std::format("  \"{}\": {},\n", s.key, value);
    }
    out += "  \"keymap\": {}\n}\n";
    return out;
}

Settings::Settings(Result<std::filesystem::path> config_dir, WriteFile write, NowFn now_ms)
    : config_dir_(std::move(config_dir)), write_(std::move(write)), now_ms_(std::move(now_ms)) {
    for (const SettingSpec& s : kSpecs) values_.push_back(s.def);
    modified_.resize(std::size(kSpecs));
}

// The file as it is now, so that what another mod wrote since it was read is kept: only
// the member being changed is written over it. A file that cannot be used is left to the
// next write to replace, as at load.
void Settings::refresh_object() {
    if (!config_dir_) return;
    const std::filesystem::path path = file();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxSettingsBytes) return;
    std::ifstream in(path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (auto parsed = Json::parse(text); parsed && parsed->is_object()) object_ = std::move(*parsed);
}

std::optional<std::string> Settings::load() {
    for (std::size_t i = 0; i < std::size(kSpecs); ++i) {
        values_[i] = kSpecs[i].def;
        modified_[i].clear();
    }
    object_ = Json::object();
    if (!config_dir_) return std::nullopt;
    const std::filesystem::path path = file();
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec || !exists) return std::nullopt;  // a missing directory or file is normal
    auto ignored = [](std::string reason) { return std::optional<std::string>("settings.json ignored: " + std::move(reason)); };
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) return ignored("cannot read it");
    if (size > kMaxSettingsBytes) return ignored("larger than 1 MiB");
    std::ifstream in(path, std::ios::binary);
    if (!in) return ignored("cannot read it");
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) return ignored("cannot read it");
    auto parsed = Json::parse(text);
    if (!parsed) return ignored(parsed.error().message);
    if (!parsed->is_object()) return ignored("not a JSON object");
    object_ = std::move(*parsed);

    // Each setting stands alone: a bad value costs only that setting.
    std::optional<std::string> first_bad;
    int more_bad = 0;
    const Json* modified = object_.get(kModifiedKey);
    for (std::size_t i = 0; i < std::size(kSpecs); ++i) {
        const SettingSpec& spec = kSpecs[i];
        if (structured(spec.type)) continue;  // read by its owner through `raw`
        if (const Json* j = object_.get(spec.key)) {
            if (const auto v = read_value(spec, *j)) {
                values_[i] = *v;
            } else if (!first_bad) {
                first_bad = expectation(spec);
            } else {
                ++more_bad;
            }
        }
        if (modified != nullptr && modified->is_object()) {
            if (const Json* t = modified->get(spec.key); t != nullptr && t->is_string()) modified_[i] = normalize_time(t->as_string());
        }
    }
    if (!first_bad) return std::nullopt;
    std::string warning = "settings.json: " + *first_bad;
    if (more_bad > 0) warning += std::format(" (and {} more bad value{})", more_bad, more_bad == 1 ? "" : "s");
    return warning;
}

std::int64_t Settings::value(std::string_view key) const {
    const auto i = index_of(key);
    return i ? values_[*i] : 0;
}

// The schema row of scalar setting `key`, when `value` is one it can take.
Result<std::size_t> Settings::scalar_row(std::string_view key, std::int64_t value) const {
    const auto i = index_of(key);
    if (!i) return std::unexpected(make_error(ErrorCode::internal, "unknown setting " + std::string(key)));
    const SettingSpec& spec = kSpecs[*i];
    if (structured(spec.type)) return std::unexpected(make_error(ErrorCode::internal, std::string(spec.key) + " is not a single value"));
    if (value < spec.min || value > spec.max) return std::unexpected(make_error(ErrorCode::internal, expectation(spec)));
    return *i;
}

Status Settings::override(std::string_view key, std::int64_t value) {
    const auto i = scalar_row(key, value);
    if (!i) return std::unexpected(i.error());
    values_[*i] = value;  // the file is written from object_, which this leaves alone
    return {};
}

Status Settings::set(std::string_view key, std::int64_t value) {
    const auto i = scalar_row(key, value);
    if (!i) return std::unexpected(i.error());
    const SettingSpec& spec = kSpecs[*i];
    if (value == values_[*i]) return {};  // nothing is written for an unchanged value
    values_[*i] = value;
    const auto now = now_ms_ ? now_ms_()
                             : std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
    modified_[*i] = format_time(now);
    if (!config_dir_) return std::unexpected(config_dir_.error());
    refresh_object();
    object_.set(std::string(spec.key), spec.type == T::boolean ? Json(value != 0)
                                       : spec.type == T::choice   ? Json(spec.choices[static_cast<std::size_t>(value)])
                                                                  : Json(value));
    Json* modified = object_.get(kModifiedKey);
    if (modified == nullptr || !modified->is_object()) modified = &object_.set(std::string(kModifiedKey), Json::object());
    modified->set(std::string(spec.key), Json(modified_[*i]));
    return write_file();
}

Status Settings::set_raw(std::string_view key, std::optional<Json> value) {
    refresh_object();
    const Json* current = object_.get(key);
    if (value ? (current != nullptr && *current == *value) : current == nullptr) return {};  // unchanged
    if (value) {
        object_.set(std::string(key), std::move(*value));
    } else {
        object_.erase(key);
    }
    if (!config_dir_) return std::unexpected(config_dir_.error());
    return write_file();
}

std::vector<const SettingSpec*> Settings::recent(std::size_t max) const {
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < std::size(kSpecs); ++i) {
        if (!modified_[i].empty()) order.push_back(i);
    }
    // Newest first; settings changed in the same millisecond keep the schema order.
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return modified_[a] > modified_[b]; });
    if (order.size() > max) order.resize(max);
    std::vector<const SettingSpec*> out;
    out.reserve(order.size());
    for (const std::size_t i : order) out.push_back(&kSpecs[i]);
    return out;
}

Status Settings::write_file() {
    std::error_code ec;
    std::filesystem::create_directories(*config_dir_, ec);
    if (ec) {
        // A file in the way (EEXIST, ENOTDIR) is `io`; permission and space keep their codes.
        ErrorCode code = ErrorCode::io;
        if (ec.value() == EACCES || ec.value() == EPERM || ec.value() == EROFS) code = ErrorCode::permission;
        if (ec.value() == ENOSPC) code = ErrorCode::no_space;
        return std::unexpected(Error{code, ec.value(), "create " + config_dir_->string() + ": " + ec.message()});
    }
    const std::string text = object_.dump() + "\n";
    const ContentProducer produce = [&](const ByteSink& sink) { return sink(std::as_bytes(std::span(text.data(), text.size()))); };
    // Through a symlink to the real file (a dotfile manager's link stays a link), keeping
    // that file's permissions; a file that does not exist yet gets the defaults.
    const auto real = resolve_real_path(file());
    const std::filesystem::path target = real ? *real : file();
    if (write_) return write_(target, produce);
    const bool exists = std::filesystem::exists(target, ec);
    return exists ? write_atomically(target, produce, target) : write_atomically(target, produce);
}

}  // namespace mod
